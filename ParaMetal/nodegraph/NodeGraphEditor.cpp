#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePointsParams.hpp"
#include "NodeTransformParams.hpp"

#include "NodeGraph.hpp"
#include "nodegraph/ui/scene/NodeGraphSceneUtils.hpp"
#include "nodegraph/NodeParamUtils.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

NodeGraphEditor::NodeGraphEditor(NodeGraph* graphPtr)
    : bridge(graphPtr) {
}

NodeGraphEditor::NodeGraphEditor(NodeGraph& graphRef)
    : bridge(&graphRef) {
}

void NodeGraphEditor::setGraph(NodeGraph* graphPtr) {
    bridge = graphPtr;
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
        const NodeGraphSocket* socket = node.input(valueType);
        return socket ? socket->id : NodeGraphSocketId{};
    };
    const auto outputSocketByType = [](const NodeGraphNode& node, NodeGraphValueType valueType) {
        const NodeGraphSocket* socket = node.output(valueType);
        return socket ? socket->id : NodeGraphSocketId{};
    };
    const auto firstOutputSocket = [](const NodeGraphNode& node) {
        return node.outputs.empty() ? NodeGraphSocketId{} : node.outputs.front().id;
    };
    const auto inputSocketByName = [](const NodeGraphNode& node, const char* name) {
        const NodeGraphSocket* socket = node.input(name);
        return socket ? socket->id : NodeGraphSocketId{};
    };

    constexpr float columnSpacing = 100.0f;
    constexpr float rowSpacing = 80.0f;
    constexpr float centerColumnX = 0.0f;
    constexpr float leftColumnX = centerColumnX - columnSpacing;
    constexpr float rightColumnX = centerColumnX + columnSpacing;
    constexpr float leftmostColumnX = centerColumnX - columnSpacing * 2.0f;
    constexpr float farRightColumnX = centerColumnX + columnSpacing * 2.0f;
    constexpr float row1Y = 0.0f;
    constexpr float row2Y = row1Y + rowSpacing;
    constexpr float row3Y = row2Y + rowSpacing;
    constexpr float row4Y = row3Y + rowSpacing;
    constexpr float row5Y = row4Y + rowSpacing;
    constexpr float row6Y = row5Y + rowSpacing;
    constexpr float row7Y = row6Y + rowSpacing;

    const CreatedNode receiverModel = createNode(nodegraphtypes::Model, "Receiver Model", leftColumnX, row1Y);
    const CreatedNode sourceModel = createNode(nodegraphtypes::Model, "Source Model", rightColumnX, row1Y);
    const CreatedNode receiverTransform = createNode(nodegraphtypes::Transform, "Receiver Transform", leftColumnX, row2Y);
    const CreatedNode sourceTransform = createNode(nodegraphtypes::Transform, "Source Transform", rightColumnX, row2Y);
    const CreatedNode receiverRemesh = createNode(nodegraphtypes::Remesh, "Receiver Remesh", leftColumnX, row3Y);
    const CreatedNode sourceRemesh = createNode(nodegraphtypes::Remesh, "Source Remesh", rightColumnX, row3Y);
    const CreatedNode receiverHeatModel = createNode(nodegraphtypes::HeatModel, "Receiver Heat Model", leftColumnX, row4Y);
    const CreatedNode sourceHeatModel = createNode(nodegraphtypes::HeatModel, "Source Heat Model", rightColumnX, row4Y);
    const CreatedNode points = createNode(nodegraphtypes::Points, "Points", leftmostColumnX, row1Y);
    const CreatedNode pointsTransform = createNode(nodegraphtypes::Transform, "Points Transform", leftmostColumnX, row2Y);
    const CreatedNode receiverMeshPoints = createNode(nodegraphtypes::MeshPoints, "Mesh Points", leftmostColumnX, row4Y);
    const CreatedNode sourceMeshPoints = createNode(nodegraphtypes::MeshPoints, "Mesh Points", farRightColumnX, row4Y);
    const CreatedNode merge = createNode(nodegraphtypes::Merge, "Merge", centerColumnX, row5Y);
    const CreatedNode voronoi = createNode(nodegraphtypes::Voronoi, "Voronoi", centerColumnX, row6Y);
    const CreatedNode heatSolve = createNode(nodegraphtypes::HeatSolve, "Ember", centerColumnX, row7Y);

    if (!receiverModel.id.isValid() || !receiverTransform.id.isValid() || !receiverRemesh.id.isValid() ||
        !sourceModel.id.isValid() || !sourceTransform.id.isValid() || !sourceRemesh.id.isValid() ||
        !receiverHeatModel.id.isValid() || !sourceHeatModel.id.isValid() ||
        !voronoi.id.isValid() || !heatSolve.id.isValid() ||
        !points.id.isValid() || !pointsTransform.id.isValid() ||
        !receiverMeshPoints.id.isValid() || !sourceMeshPoints.id.isValid() || !merge.id.isValid()) {
        return;
    }

    TransformNodeParams pointsTransformParams{};
    pointsTransformParams.translateX = 0.0;
    pointsTransformParams.translateY = 0.025;
    pointsTransformParams.translateZ = 0.0;
    pointsTransformParams.rotateXDegrees = 0.0;
    pointsTransformParams.rotateYDegrees = 0.0;
    pointsTransformParams.rotateZDegrees = 0.0;
    pointsTransformParams.scaleX = 1.0;
    pointsTransformParams.scaleY = 1.0;
    pointsTransformParams.scaleZ = 1.0;
    writeTransformNodeParams(*this, pointsTransform.id, pointsTransformParams);

    TransformNodeParams sourceTransformParams{};
    sourceTransformParams.translateX = 0.0;
    sourceTransformParams.translateY = -0.01;
    sourceTransformParams.translateZ = 0.0;
    sourceTransformParams.rotateXDegrees = 0.0;
    sourceTransformParams.rotateYDegrees = 0.0;
    sourceTransformParams.rotateZDegrees = 0.0;
    sourceTransformParams.scaleX = 1.0;
    sourceTransformParams.scaleY = 1.0;
    sourceTransformParams.scaleZ = 1.0;
    writeTransformNodeParams(*this, sourceTransform.id, sourceTransformParams);

    PointsNodeParams pointsParams{};
    pointsParams.pointCount = 30000;
    pointsParams.dimX = 0.25f;
    pointsParams.dimY = 0.20f;
    pointsParams.dimZ = 0.25f;
    writePointsNodeParams(*this, points.id, pointsParams);

    const NodeGraphSocketId receiverModelOutputId = outputSocketByType(receiverModel.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverTransformInputId = inputSocketByName(receiverTransform.node, "Geometry");
    const NodeGraphSocketId receiverTransformOutputId = firstOutputSocket(receiverTransform.node);
    const NodeGraphSocketId receiverRemeshInputId = inputSocketByType(receiverRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId receiverRemeshOutputId = outputSocketByType(receiverRemesh.node, NodeGraphValueType::Remesh);
    const NodeGraphSocketId sourceModelOutputId = outputSocketByType(sourceModel.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceTransformInputId = inputSocketByName(sourceTransform.node, "Geometry");
    const NodeGraphSocketId sourceTransformOutputId = firstOutputSocket(sourceTransform.node);
    const NodeGraphSocketId sourceRemeshInputId = inputSocketByType(sourceRemesh.node, NodeGraphValueType::Mesh);
    const NodeGraphSocketId sourceRemeshOutputId = outputSocketByType(sourceRemesh.node, NodeGraphValueType::Remesh);
    const NodeGraphSocketId receiverHeatModelInputId = inputSocketByType(receiverHeatModel.node, NodeGraphValueType::Remesh);
    const NodeGraphSocketId sourceHeatModelInputId = inputSocketByType(sourceHeatModel.node, NodeGraphValueType::Remesh);
    const NodeGraphSocketId voronoiRemeshesInputId = inputSocketByType(voronoi.node, NodeGraphValueType::Remesh);
    const NodeGraphSocketId voronoiPointsInputId = inputSocketByType(voronoi.node, NodeGraphValueType::Points);
    const NodeGraphSocketId heatSolveVoronoiInputId = inputSocketByType(heatSolve.node, NodeGraphValueType::Volume);
    const NodeGraphSocketId heatSolveHeatModelInputId = inputSocketByType(heatSolve.node, NodeGraphValueType::HeatModel);
    const NodeGraphSocketId receiverHeatModelOutputId = firstOutputSocket(receiverHeatModel.node);
    const NodeGraphSocketId sourceHeatModelOutputId = firstOutputSocket(sourceHeatModel.node);
    const NodeGraphSocketId voronoiOutputId = firstOutputSocket(voronoi.node);
    const NodeGraphSocketId pointsOutputId = outputSocketByType(points.node, NodeGraphValueType::Points);
    const NodeGraphSocketId pointsTransformInputId = inputSocketByName(pointsTransform.node, "Geometry");
    const NodeGraphSocketId pointsTransformOutputId = firstOutputSocket(pointsTransform.node);
    const NodeGraphSocketId receiverMeshPointsInputId = inputSocketByName(receiverMeshPoints.node, "Geometry");
    const NodeGraphSocketId receiverMeshPointsOutputId = firstOutputSocket(receiverMeshPoints.node);
    const NodeGraphSocketId sourceMeshPointsInputId = inputSocketByName(sourceMeshPoints.node, "Geometry");
    const NodeGraphSocketId sourceMeshPointsOutputId = firstOutputSocket(sourceMeshPoints.node);
    const NodeGraphSocketId mergeInputId = inputSocketByName(merge.node, "Geometry");
    const NodeGraphSocketId mergeOutputId = firstOutputSocket(merge.node);

    if (!receiverModelOutputId.isValid() || !receiverTransformInputId.isValid() || !receiverTransformOutputId.isValid() ||
        !receiverRemeshInputId.isValid() || !receiverRemeshOutputId.isValid() || !sourceModelOutputId.isValid() ||
        !sourceTransformInputId.isValid() || !sourceTransformOutputId.isValid() || !sourceRemeshInputId.isValid() ||
        !sourceRemeshOutputId.isValid() || !receiverHeatModelInputId.isValid() || !sourceHeatModelInputId.isValid() ||
        !voronoiRemeshesInputId.isValid() || !voronoiPointsInputId.isValid() ||
        !heatSolveVoronoiInputId.isValid() || !heatSolveHeatModelInputId.isValid() ||
        !receiverHeatModelOutputId.isValid() || !sourceHeatModelOutputId.isValid() || !voronoiOutputId.isValid() ||
        !pointsOutputId.isValid() || !pointsTransformInputId.isValid() || !pointsTransformOutputId.isValid() ||
        !receiverMeshPointsInputId.isValid() || !receiverMeshPointsOutputId.isValid() ||
        !sourceMeshPointsInputId.isValid() || !sourceMeshPointsOutputId.isValid() ||
        !mergeInputId.isValid() || !mergeOutputId.isValid()) {
        return;
    }

    NodeGraphParamValue receiverModelPath{};
    receiverModelPath.id = nodegraphparams::model::Path;
    receiverModelPath.type = NodeGraphParamType::String;
    receiverModelPath.stringValue = "models/heatsink.obj";
    setNodeParameter(receiverModel.id, receiverModelPath);

    NodeGraphParamValue sourceModelPath{};
    sourceModelPath.id = nodegraphparams::model::Path;
    sourceModelPath.type = NodeGraphParamType::String;
    sourceModelPath.stringValue = "models/heatsource_torus.obj";
    setNodeParameter(sourceModel.id, sourceModelPath);

    // Set receiver heat model to an adiabatic boundary.
    NodeGraphParamValue receiverHeatModelBC{};
    receiverHeatModelBC.id = nodegraphparams::heatmodel::BoundaryCondition;
    receiverHeatModelBC.type = NodeGraphParamType::Enum;
    receiverHeatModelBC.enumValue = "Adiabatic";
    setNodeParameter(receiverHeatModel.id, receiverHeatModelBC);

    // Set source heat model to a Robin Convection boundary condition with 500 HTC.
    NodeGraphParamValue sourceHeatModelBC{};
    sourceHeatModelBC.id = nodegraphparams::heatmodel::BoundaryCondition;
    sourceHeatModelBC.type = NodeGraphParamType::Enum;
    sourceHeatModelBC.enumValue = "Robin Convection";
    setNodeParameter(sourceHeatModel.id, sourceHeatModelBC);
    NodeGraphParamValue sourceHeatModelTemp{};
    sourceHeatModelTemp.id = nodegraphparams::heatmodel::DirichletTemperatureC;
    sourceHeatModelTemp.type = NodeGraphParamType::Float;
    sourceHeatModelTemp.floatValue = 100.0;
    setNodeParameter(sourceHeatModel.id, sourceHeatModelTemp);
    NodeGraphParamValue sourceHeatModelHTC{};
    sourceHeatModelHTC.id = nodegraphparams::heatmodel::HeatTransferCoefficient;
    sourceHeatModelHTC.type = NodeGraphParamType::Float;
    sourceHeatModelHTC.floatValue = 500.0;
    setNodeParameter(sourceHeatModel.id, sourceHeatModelHTC);

    NodeGraphParamValue receiverHeatModelDensity{};
    receiverHeatModelDensity.id = nodegraphparams::heatmodel::Density;
    receiverHeatModelDensity.type = NodeGraphParamType::Float;
    receiverHeatModelDensity.floatValue = 100.0;
    setNodeParameter(receiverHeatModel.id, receiverHeatModelDensity);

    NodeGraphParamValue receiverHeatModelConductivity{};
    receiverHeatModelConductivity.id = nodegraphparams::heatmodel::Conductivity;
    receiverHeatModelConductivity.type = NodeGraphParamType::Float;
    receiverHeatModelConductivity.floatValue = 5.0;
    setNodeParameter(receiverHeatModel.id, receiverHeatModelConductivity);

    NodeGraphParamValue sourceHeatModelDensity{};
    sourceHeatModelDensity.id = nodegraphparams::heatmodel::Density;
    sourceHeatModelDensity.type = NodeGraphParamType::Float;
    sourceHeatModelDensity.floatValue = 100.0;
    setNodeParameter(sourceHeatModel.id, sourceHeatModelDensity);

    NodeGraphParamValue sourceHeatModelConductivity{};
    sourceHeatModelConductivity.id = nodegraphparams::heatmodel::Conductivity;
    sourceHeatModelConductivity.type = NodeGraphParamType::Float;
    sourceHeatModelConductivity.floatValue = 5.0;
    setNodeParameter(sourceHeatModel.id, sourceHeatModelConductivity);

    NodeGraphParamValue receiverRemeshMaxEdge{};
    receiverRemeshMaxEdge.id = nodegraphparams::remesh::MaxEdgeLength;
    receiverRemeshMaxEdge.type = NodeGraphParamType::Float;
    receiverRemeshMaxEdge.floatValue = 0.005;
    setNodeParameter(receiverRemesh.id, receiverRemeshMaxEdge);

    NodeGraphParamValue sourceRemeshMaxEdge{};
    sourceRemeshMaxEdge.id = nodegraphparams::remesh::MaxEdgeLength;
    sourceRemeshMaxEdge.type = NodeGraphParamType::Float;
    sourceRemeshMaxEdge.floatValue = 0.005;
    setNodeParameter(sourceRemesh.id, sourceRemeshMaxEdge);

    NodeGraphParamValue voronoiSDFSize{};
    voronoiSDFSize.id = nodegraphparams::voronoi::SDFSize;
    voronoiSDFSize.type = NodeGraphParamType::Float;
    voronoiSDFSize.floatValue = 0.005f;
    setNodeParameter(voronoi.id, voronoiSDFSize);

    NodeGraphParamValue heatOverlayParam{};
    heatOverlayParam.id = nodegraphparams::heatsolve::ShowHeatOverlay;
    heatOverlayParam.type = NodeGraphParamType::Bool;
    heatOverlayParam.boolValue = true;
    setNodeParameter(heatSolve.id, heatOverlayParam);

    std::string errorMessage;
    connectSockets(receiverModel.id, receiverModelOutputId, receiverTransform.id, receiverTransformInputId, errorMessage);
    connectSockets(receiverTransform.id, receiverTransformOutputId, receiverRemesh.id, receiverRemeshInputId, errorMessage);
    connectSockets(receiverRemesh.id, receiverRemeshOutputId, receiverHeatModel.id, receiverHeatModelInputId, errorMessage);
    connectSockets(sourceModel.id, sourceModelOutputId, sourceTransform.id, sourceTransformInputId, errorMessage);
    connectSockets(sourceTransform.id, sourceTransformOutputId, sourceRemesh.id, sourceRemeshInputId, errorMessage);
    connectSockets(sourceRemesh.id, sourceRemeshOutputId, sourceHeatModel.id, sourceHeatModelInputId, errorMessage);
    connectSockets(receiverRemesh.id, receiverRemeshOutputId, voronoi.id, voronoiRemeshesInputId, errorMessage);
    connectSockets(sourceRemesh.id, sourceRemeshOutputId, voronoi.id, voronoiRemeshesInputId, errorMessage);
    connectSockets(receiverHeatModel.id, receiverHeatModelOutputId, heatSolve.id, heatSolveHeatModelInputId, errorMessage);
    connectSockets(sourceHeatModel.id, sourceHeatModelOutputId, heatSolve.id, heatSolveHeatModelInputId, errorMessage);
    connectSockets(points.id, pointsOutputId, pointsTransform.id, pointsTransformInputId, errorMessage);
    connectSockets(receiverRemesh.id, receiverRemeshOutputId, receiverMeshPoints.id, receiverMeshPointsInputId, errorMessage);
    connectSockets(sourceRemesh.id, sourceRemeshOutputId, sourceMeshPoints.id, sourceMeshPointsInputId, errorMessage);
    connectSockets(pointsTransform.id, pointsTransformOutputId, merge.id, mergeInputId, errorMessage);
    connectSockets(receiverMeshPoints.id, receiverMeshPointsOutputId, merge.id, mergeInputId, errorMessage);
    connectSockets(sourceMeshPoints.id, sourceMeshPointsOutputId, merge.id, mergeInputId, errorMessage);
    connectSockets(merge.id, mergeOutputId, voronoi.id, voronoiPointsInputId, errorMessage);
    connectSockets(voronoi.id, voronoiOutputId, heatSolve.id, heatSolveVoronoiInputId, errorMessage);
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

bool NodeGraphEditor::toggleNodeFrozen(NodeGraphNodeId nodeId) {
    return bridge && bridge->toggleNodeFrozen(nodeId);
}

bool NodeGraphEditor::toggleNodeDisplay(NodeGraphNodeId nodeId) {
    return bridge && bridge->toggleNodeDisplay(nodeId);
}

bool NodeGraphEditor::setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    return bridge && bridge->setNodeParameter(nodeId, parameter);
}

bool NodeGraphEditor::setNodeParameters(NodeGraphNodeId nodeId, const std::vector<NodeGraphParamValue>& parameters) {
    return bridge && bridge->setNodeParameters(nodeId, parameters);
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
    const NodeGraphEdge* existingIncomingEdge = state.edges.incomingEdge(nodeId, socketId);
    if (!existingIncomingEdge || !existingIncomingEdge->id.isValid()) {
        return false;
    }

    return removeConnection(existingIncomingEdge->id);
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

        const NodeTypeDefinition* nodeDefinition = bridge ? bridge->getRegistry().findNodeType(getNodeTypeId(copiedNode.typeId)) : nullptr;
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
            const NodeGraphNode* lhsToNode = originalState.node(lhs.toNode);
            const NodeGraphNode* rhsToNode = originalState.node(rhs.toNode);
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
        const NodeGraphNode* oldTargetNode = originalState.node(copiedEdge.toNode);
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
