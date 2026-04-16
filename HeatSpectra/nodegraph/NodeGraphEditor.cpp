#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphSceneUtils.hpp"
#include "NodePanelUtils.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool findFirstSocketByValueType(
    const std::vector<NodeGraphSocket>& sockets,
    NodeGraphValueType valueType,
    NodeGraphSocketId& outSocketId) {
    outSocketId = {};
    for (const NodeGraphSocket& socket : sockets) {
        if (socket.valueType != valueType || !socket.id.isValid()) {
            continue;
        }

        outSocketId = socket.id;
        return true;
    }

    return false;
}

}

NodeGraphEditor::NodeGraphEditor(NodeGraphBridge* bridgePtr)
    : bridge(bridgePtr) {
}

NodeGraphEditor::NodeGraphEditor(NodeGraphBridge& bridgeRef)
    : bridge(&bridgeRef) {
}

void NodeGraphEditor::setBridge(NodeGraphBridge* bridgePtr) {
    bridge = bridgePtr;
}

void NodeGraphEditor::resetToDefaultGraph() {
    if (!bridge) {
        return;
    }

    struct CreatedNode {
        NodeGraphNodeId id{};
        NodeGraphNode node{};
    };

    bridge->clear();

    const auto createNode = [this](const NodeTypeId& typeId, const std::string& title, float x, float y) {
        CreatedNode created{};
        created.id = addNode(typeId, title, x, y);
        if (created.id.isValid()) {
            bridge->getNode(created.id, created.node);
        }
        return created;
    };
    const auto inputSocketByType = [](const NodeGraphNode& node, NodeGraphValueType valueType) {
        NodeGraphSocketId socketId{};
        findFirstSocketByValueType(node.inputs, valueType, socketId);
        return socketId;
    };
    const auto outputSocketByType = [](const NodeGraphNode& node, NodeGraphValueType valueType) {
        NodeGraphSocketId socketId{};
        findFirstSocketByValueType(node.outputs, valueType, socketId);
        return socketId;
    };
    const auto firstOutputSocket = [](const NodeGraphNode& node) {
        return node.outputs.empty() ? NodeGraphSocketId{} : node.outputs.front().id;
    };

    const CreatedNode receiverModel = createNode(nodegraphtypes::Model, "Receiver Model", 30.0f, 40.0f);
    const CreatedNode receiverTransform = createNode(nodegraphtypes::Transform, "Receiver Transform", 220.0f, 40.0f);
    const CreatedNode receiverRemesh = createNode(nodegraphtypes::Remesh, "Receiver Remesh", 410.0f, 40.0f);
    const CreatedNode sourceModel = createNode(nodegraphtypes::Model, "Source Model", 30.0f, 280.0f);
    const CreatedNode sourceTransform = createNode(nodegraphtypes::Transform, "Source Transform", 220.0f, 280.0f);
    const CreatedNode sourceRemesh = createNode(nodegraphtypes::Remesh, "Source Remesh", 410.0f, 280.0f);
    const CreatedNode heatReceiver = createNode(nodegraphtypes::HeatReceiver, "", 650.0f, 40.0f);
    const CreatedNode heatSource = createNode(nodegraphtypes::HeatSource, "", 650.0f, 280.0f);
    const CreatedNode contact = createNode(nodegraphtypes::Contact, "", 860.0f, 180.0f);
    const CreatedNode voronoi = createNode(nodegraphtypes::Voronoi, "", 860.0f, 20.0f);
    const CreatedNode heatSolve = createNode(nodegraphtypes::HeatSolve, "", 1130.0f, 180.0f);

    if (!receiverModel.id.isValid() || !receiverTransform.id.isValid() || !receiverRemesh.id.isValid() ||
        !sourceModel.id.isValid() || !sourceTransform.id.isValid() || !sourceRemesh.id.isValid() ||
        !heatReceiver.id.isValid() || !heatSource.id.isValid() || !contact.id.isValid() ||
        !voronoi.id.isValid() || !heatSolve.id.isValid()) {
        return;
    }

    const NodeGraphSocketId receiverModelOutputId = outputSocketByType(receiverModel.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverTransformInputId = inputSocketByType(receiverTransform.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverTransformOutputId = outputSocketByType(receiverTransform.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverRemeshInputId = inputSocketByType(receiverRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverRemeshOutputId = outputSocketByType(receiverRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceModelOutputId = outputSocketByType(sourceModel.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceTransformInputId = inputSocketByType(sourceTransform.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceTransformOutputId = outputSocketByType(sourceTransform.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceRemeshInputId = inputSocketByType(sourceRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceRemeshOutputId = outputSocketByType(sourceRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId heatReceiverInputId = inputSocketByType(heatReceiver.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId heatSourceInputId = inputSocketByType(heatSource.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId voronoiGeometryInputId = inputSocketByType(voronoi.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId heatSolveVoronoiInputId = inputSocketByType(heatSolve.node, NodeGraphValueType::Volume);
    const NodeGraphSocketId heatSolveContactInputId = inputSocketByType(heatSolve.node, NodeGraphValueType::Field);
    const NodeGraphSocketId contactEmitterInputId = inputSocketByType(contact.node, NodeGraphValueType::Emitter);
    const NodeGraphSocketId contactReceiverInputId = inputSocketByType(contact.node, NodeGraphValueType::Receiver);
    const NodeGraphSocketId heatReceiverOutputId = firstOutputSocket(heatReceiver.node);
    const NodeGraphSocketId heatSourceOutputId = firstOutputSocket(heatSource.node);
    const NodeGraphSocketId contactOutputId = firstOutputSocket(contact.node);
    const NodeGraphSocketId voronoiOutputId = firstOutputSocket(voronoi.node);

    if (!receiverModelOutputId.isValid() || !receiverTransformInputId.isValid() || !receiverTransformOutputId.isValid() ||
        !receiverRemeshInputId.isValid() || !receiverRemeshOutputId.isValid() || !sourceModelOutputId.isValid() ||
        !sourceTransformInputId.isValid() || !sourceTransformOutputId.isValid() || !sourceRemeshInputId.isValid() ||
        !sourceRemeshOutputId.isValid() || !heatReceiverInputId.isValid() || !heatSourceInputId.isValid() ||
        !voronoiGeometryInputId.isValid() || !heatSolveVoronoiInputId.isValid() || !heatSolveContactInputId.isValid() ||
        !contactEmitterInputId.isValid() || !contactReceiverInputId.isValid() || !heatReceiverOutputId.isValid() ||
        !heatSourceOutputId.isValid() || !contactOutputId.isValid() || !voronoiOutputId.isValid()) {
        return;
    }

    NodeGraphParamValue receiverModelPath{};
    receiverModelPath.id = nodegraphparams::model::Path;
    receiverModelPath.type = NodeGraphParamType::String;
    receiverModelPath.stringValue = "models/channel_tube.obj";
    setNodeParameter(receiverModel.id, receiverModelPath);

    NodeGraphParamValue sourceModelPath{};
    sourceModelPath.id = nodegraphparams::model::Path;
    sourceModelPath.type = NodeGraphParamType::String;
    sourceModelPath.stringValue = "models/heatsource_tube.obj";
    setNodeParameter(sourceModel.id, sourceModelPath);

    std::string errorMessage;
    connectSockets(receiverModel.id, receiverModelOutputId, receiverTransform.id, receiverTransformInputId, errorMessage);
    connectSockets(receiverTransform.id, receiverTransformOutputId, receiverRemesh.id, receiverRemeshInputId, errorMessage);
    connectSockets(receiverRemesh.id, receiverRemeshOutputId, voronoi.id, voronoiGeometryInputId, errorMessage);
    connectSockets(receiverRemesh.id, receiverRemeshOutputId, heatReceiver.id, heatReceiverInputId, errorMessage);
    connectSockets(sourceModel.id, sourceModelOutputId, sourceTransform.id, sourceTransformInputId, errorMessage);
    connectSockets(sourceTransform.id, sourceTransformOutputId, sourceRemesh.id, sourceRemeshInputId, errorMessage);
    connectSockets(sourceRemesh.id, sourceRemeshOutputId, heatSource.id, heatSourceInputId, errorMessage);
    connectSockets(heatSource.id, heatSourceOutputId, contact.id, contactEmitterInputId, errorMessage);
    connectSockets(heatReceiver.id, heatReceiverOutputId, contact.id, contactReceiverInputId, errorMessage);
    connectSockets(voronoi.id, voronoiOutputId, heatSolve.id, heatSolveVoronoiInputId, errorMessage);
    connectSockets(contact.id, contactOutputId, heatSolve.id, heatSolveContactInputId, errorMessage);
}

NodeGraphNodeId NodeGraphEditor::addNode(const NodeTypeId& typeId, const std::string& title, float x, float y) {
    if (!bridge) {
        return {};
    }

    return bridge->addNode(typeId, title, x, y);
}

bool NodeGraphEditor::removeNode(NodeGraphNodeId nodeId) {
    return bridge && bridge->removeNode(nodeId);
}

bool NodeGraphEditor::moveNode(NodeGraphNodeId nodeId, float x, float y) {
    return bridge && bridge->moveNode(nodeId, x, y);
}

bool NodeGraphEditor::setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    return bridge && bridge->setNodeParameter(nodeId, parameter);
}

bool NodeGraphEditor::updateNodeParameter(
    NodeGraphNodeId nodeId,
    uint32_t paramId,
    const std::function<bool(NodeGraphParamValue&)>& updater) {
    if (!bridge || !nodeId.isValid() || !updater) {
        return false;
    }

    NodeGraphNode node{};
    if (!bridge->getNode(nodeId, node)) {
        return false;
    }

    const NodeGraphParamValue* existingParameter = findNodeParamValue(node, paramId);
    if (!existingParameter) {
        return false;
    }

    NodeGraphParamValue updatedParameter = *existingParameter;
    if (!updater(updatedParameter)) {
        return false;
    }

    return setNodeParameter(nodeId, updatedParameter);
}

bool NodeGraphEditor::connectSockets(
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    std::string& errorMessage,
    bool replaceExistingInput) {
    if (!bridge) {
        errorMessage = "Bridge unavailable.";
        return false;
    }

    if (!bridge->connectSockets(fromNode, fromSocket, toNode, toSocket, errorMessage, replaceExistingInput)) {
        return false;
    }
    return true;
}

bool NodeGraphEditor::removeConnection(NodeGraphEdgeId edgeId) {
    return bridge && bridge->removeConnection(edgeId);
}

bool NodeGraphEditor::disconnectIncomingInput(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    if (!bridge || !nodeId.isValid() || !socketId.isValid()) {
        return false;
    }

    const NodeGraphState state = bridge->state();
    const NodeGraphEdgeId existingIncomingEdge = nodegraphsceneutils::findIncomingEdgeForInput(state, nodeId, socketId);
    if (!existingIncomingEdge.isValid()) {
        return false;
    }

    return removeConnection(existingIncomingEdge);
}

bool NodeGraphEditor::pasteCopiedNodes(
    const std::vector<CopiedNode>& copiedNodes,
    const std::vector<CopiedEdge>& copiedEdges,
    float positionOffset,
    std::vector<NodeGraphNodeId>& outCreatedNodeIds) {
    outCreatedNodeIds.clear();
    if (!bridge || copiedNodes.empty()) {
        return false;
    }

    std::unordered_map<uint32_t, NodeGraphNodeId> newNodeIdBySourceNodeId;
    std::unordered_map<uint32_t, NodeGraphSocketId> newOutputSocketBySourceOutputSocket;
    outCreatedNodeIds.reserve(copiedNodes.size());

    for (const CopiedNode& copiedNode : copiedNodes) {
        const NodeGraphNodeId newNodeId = addNode(
            copiedNode.typeId,
            copiedNode.title,
            copiedNode.x + positionOffset,
            copiedNode.y + positionOffset);
        if (!newNodeId.isValid()) {
            continue;
        }

        newNodeIdBySourceNodeId[copiedNode.sourceNodeId.value] = newNodeId;
        outCreatedNodeIds.push_back(newNodeId);

        NodeGraphNode newNode{};
        if (!bridge->getNode(newNodeId, newNode)) {
            continue;
        }

        const std::size_t outputSocketCount = std::min(copiedNode.outputSocketIds.size(), newNode.outputs.size());
        for (std::size_t index = 0; index < outputSocketCount; ++index) {
            newOutputSocketBySourceOutputSocket[copiedNode.outputSocketIds[index].value] = newNode.outputs[index].id;
        }

        const NodeTypeDefinition* nodeDefinition = NodeGraphRegistry::findNodeById(getNodeTypeId(copiedNode.typeId));
        for (const NodeGraphParamValue& originalParameter : copiedNode.parameters) {
            NodeGraphParamValue parameter = originalParameter;

            if (nodeDefinition) {
                const NodeGraphParamDefinition* parameterDefinition = findNodeParamDefinition(*nodeDefinition, parameter.id);
                if (parameterDefinition && parameterDefinition->isAction) {
                    parameter = makeNodeGraphParamValue(*parameterDefinition);
                }
            }

            setNodeParameter(newNodeId, parameter);
        }
    }

    if (outCreatedNodeIds.empty()) {
        return false;
    }

    std::vector<CopiedEdge> sortedEdges = copiedEdges;
    const NodeGraphState originalState = bridge->state();
    std::sort(
        sortedEdges.begin(),
        sortedEdges.end(),
        [&originalState](const CopiedEdge& lhs, const CopiedEdge& rhs) {
            const NodeGraphNode* lhsToNode = nodegraphsceneutils::findStateNodeById(originalState, lhs.toNode);
            const NodeGraphNode* rhsToNode = nodegraphsceneutils::findStateNodeById(originalState, rhs.toNode);
            const int lhsSocketIndex = lhsToNode ? nodegraphsceneutils::findSocketIndexById(lhsToNode->inputs, lhs.toSocket) : -1;
            const int rhsSocketIndex = rhsToNode ? nodegraphsceneutils::findSocketIndexById(rhsToNode->inputs, rhs.toSocket) : -1;
            if (lhs.toNode.value != rhs.toNode.value) {
                return lhs.toNode.value < rhs.toNode.value;
            }
            return lhsSocketIndex < rhsSocketIndex;
        });

    for (const CopiedEdge& copiedEdge : sortedEdges) {
        const auto fromNodeIt = newNodeIdBySourceNodeId.find(copiedEdge.fromNode.value);
        const auto toNodeIt = newNodeIdBySourceNodeId.find(copiedEdge.toNode.value);
        if (fromNodeIt == newNodeIdBySourceNodeId.end() || toNodeIt == newNodeIdBySourceNodeId.end()) {
            continue;
        }

        const auto fromSocketIt = newOutputSocketBySourceOutputSocket.find(copiedEdge.fromSocket.value);
        if (fromSocketIt == newOutputSocketBySourceOutputSocket.end()) {
            continue;
        }

        NodeGraphSocketId targetInputSocket{};
        const NodeGraphNode* oldTargetNode = nodegraphsceneutils::findStateNodeById(originalState, copiedEdge.toNode);
        if (!oldTargetNode) {
            continue;
        }

        const int oldInputSocketIndex = nodegraphsceneutils::findSocketIndexById(oldTargetNode->inputs, copiedEdge.toSocket);
        if (oldInputSocketIndex < 0) {
            continue;
        }

        NodeGraphNode newTargetNode{};
        if (!bridge->getNode(toNodeIt->second, newTargetNode)) {
            continue;
        }

        targetInputSocket = nodegraphsceneutils::socketByIndex(newTargetNode.inputs, oldInputSocketIndex);
        if (!targetInputSocket.isValid()) {
            const NodeGraphSocket& oldInputSocket = oldTargetNode->inputs[static_cast<std::size_t>(oldInputSocketIndex)];
            const int oldOrdinal = nodegraphsceneutils::valueTypeOrdinalAtInputIndex(*oldTargetNode, oldInputSocketIndex);
            if (oldOrdinal >= 0) {
                const std::vector<NodeGraphSocketId> matchingSockets =
                    nodegraphsceneutils::matchingInputSocketsByType(newTargetNode, oldInputSocket.valueType);
                if (oldOrdinal < static_cast<int>(matchingSockets.size())) {
                    targetInputSocket = matchingSockets[static_cast<std::size_t>(oldOrdinal)];
                }
            }
        }

        if (!targetInputSocket.isValid()) {
            continue;
        }

        std::string errorMessage;
        connectSockets(
            fromNodeIt->second,
            fromSocketIt->second,
            toNodeIt->second,
            targetInputSocket,
            errorMessage);
    }

    return true;
}
