#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphSceneUtils.hpp"
#include "NodePanelUtils.hpp"
#include "NodeTransformParams.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool findFirstDownstreamTransformNode(const NodeGraphState& state, NodeGraphNodeId startNodeId, NodeGraphNodeId& outTransformNodeId) {
    outTransformNodeId = {};
    if (!startNodeId.isValid()) {
        return false;
    }

    std::vector<NodeGraphNodeId> stack{startNodeId};
    std::unordered_set<uint32_t> visitedNodeIds;
    visitedNodeIds.insert(startNodeId.value);

    while (!stack.empty()) {
        const NodeGraphNodeId currentNodeId = stack.back();
        stack.pop_back();

        for (const NodeGraphEdge& edge : state.edges) {
            if (edge.fromNode != currentNodeId || !edge.toNode.isValid()) {
                continue;
            }
            if (!visitedNodeIds.insert(edge.toNode.value).second) {
                continue;
            }

            const NodeGraphNode* nextNode = findNodeInState(state, edge.toNode);
            if (!nextNode) {
                continue;
            }
            if (getNodeTypeId(nextNode->typeId) == nodegraphtypes::Transform) {
                outTransformNodeId = nextNode->id;
                return true;
            }

            stack.push_back(nextNode->id);
        }
    }

    return false;
}

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

    bridge->clear();

    const NodeGraphNodeId receiverModelNode = addNode(nodegraphtypes::Model, "Receiver Model", 30.0f, 40.0f);
    const NodeGraphNodeId receiverTransformNode = addNode(nodegraphtypes::Transform, "Receiver Transform", 220.0f, 40.0f);
    const NodeGraphNodeId receiverRemeshNode = addNode(nodegraphtypes::Remesh, "Receiver Remesh", 410.0f, 40.0f);
    const NodeGraphNodeId sourceModelNode = addNode(nodegraphtypes::Model, "Source Model", 30.0f, 280.0f);
    const NodeGraphNodeId sourceTransformNode = addNode(nodegraphtypes::Transform, "Source Transform", 220.0f, 280.0f);
    const NodeGraphNodeId sourceRemeshNode = addNode(nodegraphtypes::Remesh, "Source Remesh", 410.0f, 280.0f);
    const NodeGraphNodeId heatReceiverNode = addNode(nodegraphtypes::HeatReceiver, "", 650.0f, 40.0f);
    const NodeGraphNodeId heatSourceNode = addNode(nodegraphtypes::HeatSource, "", 650.0f, 280.0f);
    const NodeGraphNodeId contactNode = addNode(nodegraphtypes::Contact, "", 860.0f, 180.0f);
    const NodeGraphNodeId voronoiNode = addNode(nodegraphtypes::Voronoi, "", 860.0f, 20.0f);
    const NodeGraphNodeId heatSolveNode = addNode(nodegraphtypes::HeatSolve, "", 1130.0f, 180.0f);

    NodeGraphNode receiverModel{};
    NodeGraphNode receiverTransform{};
    NodeGraphNode receiverRemesh{};
    NodeGraphNode sourceModel{};
    NodeGraphNode sourceTransform{};
    NodeGraphNode sourceRemesh{};
    NodeGraphNode heatReceiver{};
    NodeGraphNode heatSource{};
    NodeGraphNode contact{};
    NodeGraphNode voronoi{};
    NodeGraphNode heatSolve{};

    if (!bridge->getNode(receiverModelNode, receiverModel) ||
        !bridge->getNode(receiverTransformNode, receiverTransform) ||
        !bridge->getNode(receiverRemeshNode, receiverRemesh) ||
        !bridge->getNode(sourceModelNode, sourceModel) ||
        !bridge->getNode(sourceTransformNode, sourceTransform) ||
        !bridge->getNode(sourceRemeshNode, sourceRemesh) ||
        !bridge->getNode(heatReceiverNode, heatReceiver) ||
        !bridge->getNode(heatSourceNode, heatSource) ||
        !bridge->getNode(contactNode, contact) ||
        !bridge->getNode(voronoiNode, voronoi) ||
        !bridge->getNode(heatSolveNode, heatSolve)) {
        return;
    }

    NodeGraphSocketId receiverModelOutputId{};
    NodeGraphSocketId receiverTransformInputId{};
    NodeGraphSocketId receiverTransformOutputId{};
    NodeGraphSocketId receiverRemeshInputId{};
    NodeGraphSocketId receiverRemeshOutputId{};
    NodeGraphSocketId sourceModelOutputId{};
    NodeGraphSocketId sourceTransformInputId{};
    NodeGraphSocketId sourceTransformOutputId{};
    NodeGraphSocketId sourceRemeshInputId{};
    NodeGraphSocketId sourceRemeshOutputId{};
    NodeGraphSocketId heatReceiverInputId{};
    NodeGraphSocketId heatSourceInputId{};
    NodeGraphSocketId voronoiGeometryInputId{};
    NodeGraphSocketId heatSolveVoronoiInputId{};
    NodeGraphSocketId heatSolveContactInputId{};

    if (!findFirstSocketByValueType(receiverModel.outputs, NodeGraphValueType::Mesh, receiverModelOutputId) ||
        !findFirstSocketByValueType(receiverTransform.inputs, NodeGraphValueType::Mesh, receiverTransformInputId) ||
        !findFirstSocketByValueType(receiverTransform.outputs, NodeGraphValueType::Mesh, receiverTransformOutputId) ||
        !findFirstSocketByValueType(receiverRemesh.inputs, NodeGraphValueType::Mesh, receiverRemeshInputId) ||
        !findFirstSocketByValueType(receiverRemesh.outputs, NodeGraphValueType::Mesh, receiverRemeshOutputId) ||
        !findFirstSocketByValueType(sourceModel.outputs, NodeGraphValueType::Mesh, sourceModelOutputId) ||
        !findFirstSocketByValueType(sourceTransform.inputs, NodeGraphValueType::Mesh, sourceTransformInputId) ||
        !findFirstSocketByValueType(sourceTransform.outputs, NodeGraphValueType::Mesh, sourceTransformOutputId) ||
        !findFirstSocketByValueType(sourceRemesh.inputs, NodeGraphValueType::Mesh, sourceRemeshInputId) ||
        !findFirstSocketByValueType(sourceRemesh.outputs, NodeGraphValueType::Mesh, sourceRemeshOutputId) ||
        !findFirstSocketByValueType(heatReceiver.inputs, NodeGraphValueType::Mesh, heatReceiverInputId) ||
        !findFirstSocketByValueType(heatSource.inputs, NodeGraphValueType::Mesh, heatSourceInputId) ||
        !findFirstSocketByValueType(voronoi.inputs, NodeGraphValueType::Mesh, voronoiGeometryInputId) ||
        !findFirstSocketByValueType(heatSolve.inputs, NodeGraphValueType::Volume, heatSolveVoronoiInputId) ||
        !findFirstSocketByValueType(heatSolve.inputs, NodeGraphValueType::Field, heatSolveContactInputId) ||
        heatReceiver.outputs.empty() || heatSource.outputs.empty() || contact.outputs.empty() || voronoi.outputs.empty()) {
        return;
    }

    const NodeGraphSocketId contactEmitterInputId = [&]() {
        NodeGraphSocketId socketId{};
        findFirstSocketByValueType(contact.inputs, NodeGraphValueType::Emitter, socketId);
        return socketId;
    }();
    const NodeGraphSocketId contactReceiverInputId = [&]() {
        NodeGraphSocketId socketId{};
        findFirstSocketByValueType(contact.inputs, NodeGraphValueType::Receiver, socketId);
        return socketId;
    }();
    if (!contactEmitterInputId.isValid() || !contactReceiverInputId.isValid()) {
        return;
    }

    NodeGraphParamValue receiverModelPath{};
    receiverModelPath.id = nodegraphparams::model::Path;
    receiverModelPath.type = NodeGraphParamType::String;
    receiverModelPath.stringValue = "models/channel_tube.obj";
    setNodeParameter(receiverModelNode, receiverModelPath);

    NodeGraphParamValue sourceModelPath{};
    sourceModelPath.id = nodegraphparams::model::Path;
    sourceModelPath.type = NodeGraphParamType::String;
    sourceModelPath.stringValue = "models/heatsource_tube.obj";
    setNodeParameter(sourceModelNode, sourceModelPath);

    std::string errorMessage;
    connectSockets(receiverModelNode, receiverModelOutputId, receiverTransformNode, receiverTransformInputId, errorMessage);
    connectSockets(receiverTransformNode, receiverTransformOutputId, receiverRemeshNode, receiverRemeshInputId, errorMessage);
    connectSockets(receiverRemeshNode, receiverRemeshOutputId, voronoiNode, voronoiGeometryInputId, errorMessage);
    connectSockets(receiverRemeshNode, receiverRemeshOutputId, heatReceiverNode, heatReceiverInputId, errorMessage);
    connectSockets(sourceModelNode, sourceModelOutputId, sourceTransformNode, sourceTransformInputId, errorMessage);
    connectSockets(sourceTransformNode, sourceTransformOutputId, sourceRemeshNode, sourceRemeshInputId, errorMessage);
    connectSockets(sourceRemeshNode, sourceRemeshOutputId, heatSourceNode, heatSourceInputId, errorMessage);
    connectSockets(heatSourceNode, heatSource.outputs[0].id, contactNode, contactEmitterInputId, errorMessage);
    connectSockets(heatReceiverNode, heatReceiver.outputs[0].id, contactNode, contactReceiverInputId, errorMessage);
    connectSockets(voronoiNode, voronoi.outputs[0].id, heatSolveNode, heatSolveVoronoiInputId, errorMessage);
    connectSockets(contactNode, contact.outputs[0].id, heatSolveNode, heatSolveContactInputId, errorMessage);
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

    NodeGraphNode targetNode{};
    if (!bridge->getNode(toNode, targetNode)) {
        return true;
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

bool NodeGraphEditor::ensureTransformForModelNode(NodeGraphNodeId modelNodeId, NodeGraphNodeId& outTransformNodeId) {
    outTransformNodeId = {};
    if (!bridge || !modelNodeId.isValid()) {
        return false;
    }

    const NodeGraphState state = bridge->state();
    if (findFirstDownstreamTransformNode(state, modelNodeId, outTransformNodeId)) {
        return true;
    }

    const NodeGraphNode* modelNode = findNodeInState(state, modelNodeId);
    if (!modelNode || getNodeTypeId(modelNode->typeId) != nodegraphtypes::Model) {
        return false;
    }

    NodeGraphSocketId modelOutputSocketId{};
    if (!findFirstSocketByValueType(modelNode->outputs, NodeGraphValueType::Mesh, modelOutputSocketId)) {
        return false;
    }

    std::vector<NodeGraphEdge> outgoingEdges;
    outgoingEdges.reserve(state.edges.size());
    for (const NodeGraphEdge& edge : state.edges) {
        if (edge.fromNode == modelNodeId && edge.fromSocket == modelOutputSocketId) {
            outgoingEdges.push_back(edge);
        }
    }

    const NodeGraphNodeId transformNodeId = addNode(
        nodegraphtypes::Transform,
        "",
        modelNode->x + 180.0f,
        modelNode->y);
    if (!transformNodeId.isValid()) {
        return false;
    }

    NodeGraphNode transformNode{};
    if (!bridge->getNode(transformNodeId, transformNode)) {
        return false;
    }

    NodeGraphSocketId transformInputSocketId{};
    NodeGraphSocketId transformOutputSocketId{};
    if (!findFirstSocketByValueType(transformNode.inputs, NodeGraphValueType::Mesh, transformInputSocketId) ||
        !findFirstSocketByValueType(transformNode.outputs, NodeGraphValueType::Mesh, transformOutputSocketId)) {
        return false;
    }

    std::string errorMessage;
    if (!connectSockets(
            modelNodeId,
            modelOutputSocketId,
            transformNodeId,
            transformInputSocketId,
            errorMessage,
            true)) {
        return false;
    }

    for (const NodeGraphEdge& edge : outgoingEdges) {
        errorMessage.clear();
        if (!connectSockets(
                transformNodeId,
                transformOutputSocketId,
                edge.toNode,
                edge.toSocket,
                errorMessage,
                true)) {
            return false;
        }
    }

    outTransformNodeId = transformNodeId;
    return true;
}

bool NodeGraphEditor::readTransformNodeValues(
    NodeGraphNodeId nodeId,
    glm::vec3& outTranslation,
    glm::vec3& outRotationDegrees) const {
    if (!bridge) {
        return false;
    }

    NodeGraphNode node{};
    if (!bridge->getNode(nodeId, node) ||
        getNodeTypeId(node.typeId) != nodegraphtypes::Transform) {
        return false;
    }

    const TransformNodeParams params = readTransformNodeParams(node);
    outTranslation.x = static_cast<float>(params.translateX);
    outTranslation.y = static_cast<float>(params.translateY);
    outTranslation.z = static_cast<float>(params.translateZ);
    outRotationDegrees.x = static_cast<float>(params.rotateXDegrees);
    outRotationDegrees.y = static_cast<float>(params.rotateYDegrees);
    outRotationDegrees.z = static_cast<float>(params.rotateZDegrees);
    return true;
}

bool NodeGraphEditor::writeTransformTranslation(NodeGraphNodeId nodeId, const glm::vec3& translation) {
    if (!bridge || !nodeId.isValid()) {
        return false;
    }

    NodeGraphNode node{};
    if (!bridge->getNode(nodeId, node) || getNodeTypeId(node.typeId) != nodegraphtypes::Transform) {
        return false;
    }

    TransformNodeParams params = readTransformNodeParams(node);
    params.translateX = translation.x;
    params.translateY = translation.y;
    params.translateZ = translation.z;
    return writeTransformNodeParams(*this, nodeId, params);
}

bool NodeGraphEditor::writeTransformRotation(NodeGraphNodeId nodeId, const glm::vec3& rotationDegrees) {
    if (!bridge || !nodeId.isValid()) {
        return false;
    }

    NodeGraphNode node{};
    if (!bridge->getNode(nodeId, node) || getNodeTypeId(node.typeId) != nodegraphtypes::Transform) {
        return false;
    }

    TransformNodeParams params = readTransformNodeParams(node);
    params.rotateXDegrees = rotationDegrees.x;
    params.rotateYDegrees = rotationDegrees.y;
    params.rotateZDegrees = rotationDegrees.z;
    return writeTransformNodeParams(*this, nodeId, params);
}
