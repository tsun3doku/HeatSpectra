#include "NodeGraphBridge.hpp"

#include "NodeGraphValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

NodeGraphEdgeId findIncomingEdgeId(const NodeGraphDocument& doc, NodeGraphNodeId toNode, NodeGraphSocketId toSocket) {
    for (const NodeGraphEdge& edge : doc.getEdges()) {
        if (edge.toNode == toNode && edge.toSocket == toSocket) {
            return edge.id;
        }
    }

    return {};
}

const NodeGraphEdge* findEdgeById(const NodeGraphDocument& doc, NodeGraphEdgeId edgeId) {
    for (const NodeGraphEdge& edge : doc.getEdges()) {
        if (edge.id == edgeId) {
            return &edge;
        }
    }

    return nullptr;
}

NodeGraphSocketId findFirstInputSocketByValueType(const NodeGraphNode& node, NodeGraphValueType valueType) {
    for (const NodeGraphSocket& socket : node.inputs) {
        if (socket.valueType == valueType && socket.id.isValid()) {
            return socket.id;
        }
    }

    return {};
}

NodeGraphSocketId findFirstInputSocketByName(const NodeGraphNode& node, const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }

    for (const NodeGraphSocket& socket : node.inputs) {
        if (socket.id.isValid() && socket.name == name) {
            return socket.id;
        }
    }

    return {};
}

std::string makeHeatSolveSocketName(NodeGraphValueType valueType, std::size_t socketOrdinal) {
    const char* baseName = "Input";
    if (valueType == NodeGraphValueType::ContactPair) {
        baseName = "Contact Pair";
    } else if (valueType == NodeGraphValueType::HeatReceiver) {
        baseName = "Receiver";
    } else if (valueType == NodeGraphValueType::HeatSource) {
        baseName = "Source";
    }
    if (socketOrdinal <= 1) {
        return baseName;
    }

    return std::string(baseName) + " " + std::to_string(socketOrdinal);
}

}

NodeGraphBridge::NodeGraphBridge() {
    resetToDefaultGraph();
}

void NodeGraphBridge::resetToDefaultGraph() {
    std::lock_guard<std::mutex> lock(mutex);

    document.clear();

    const NodeGraphNodeId receiverModelNode = document.addNode(nodegraphtypes::Model, "Receiver Model", 30.0f, 40.0f);
    const NodeGraphNodeId sourceModelNode = document.addNode(nodegraphtypes::Model, "Source Model", 30.0f, 240.0f);
    const NodeGraphNodeId receiverRemeshNode = document.addNode(nodegraphtypes::Remesh, "Receiver Remesh", 240.0f, 40.0f);
    const NodeGraphNodeId sourceRemeshNode = document.addNode(nodegraphtypes::Remesh, "Source Remesh", 240.0f, 240.0f);
    const NodeGraphNodeId heatReceiverNode = document.addNode(nodegraphtypes::HeatReceiver, "", 470.0f, 40.0f);
    const NodeGraphNodeId heatSourceNode = document.addNode(nodegraphtypes::HeatSource, "", 470.0f, 240.0f);
    const NodeGraphNodeId contactPairNode = document.addNode(nodegraphtypes::ContactPair, "", 650.0f, 140.0f);
    const NodeGraphNodeId heatSolveNode = document.addNode(nodegraphtypes::HeatSolve, "", 870.0f, 140.0f);

    const NodeGraphNode* receiverModelPtr = document.findNode(receiverModelNode);
    const NodeGraphNode* sourceModelPtr = document.findNode(sourceModelNode);
    const NodeGraphNode* receiverRemeshPtr = document.findNode(receiverRemeshNode);
    const NodeGraphNode* sourceRemeshPtr = document.findNode(sourceRemeshNode);
    const NodeGraphNode* heatReceiverPtr = document.findNode(heatReceiverNode);
    const NodeGraphNode* heatSourcePtr = document.findNode(heatSourceNode);
    const NodeGraphNode* contactPairPtr = document.findNode(contactPairNode);
    const NodeGraphNode* heatSolvePtr = document.findNode(heatSolveNode);

    if (receiverModelPtr && sourceModelPtr &&
        receiverRemeshPtr && sourceRemeshPtr &&
        heatReceiverPtr && heatSourcePtr && contactPairPtr && heatSolvePtr &&
        !receiverModelPtr->outputs.empty() && !sourceModelPtr->outputs.empty() &&
        !receiverRemeshPtr->inputs.empty() && !receiverRemeshPtr->outputs.empty() &&
        !sourceRemeshPtr->inputs.empty() && !sourceRemeshPtr->outputs.empty() &&
        !heatReceiverPtr->inputs.empty() && !heatReceiverPtr->outputs.empty() &&
        !heatSourcePtr->inputs.empty() && !heatSourcePtr->outputs.empty() &&
        contactPairPtr->inputs.size() >= 2 && !contactPairPtr->outputs.empty()) {
        const NodeGraphSocketId heatSolveContactPairInputId =
            findFirstInputSocketByValueType(*heatSolvePtr, NodeGraphValueType::ContactPair);
        const NodeGraphSocketId contactPairEmitterInputId =
            findFirstInputSocketByName(*contactPairPtr, "Emitter");
        const NodeGraphSocketId contactPairReceiverInputId =
            findFirstInputSocketByName(*contactPairPtr, "Receiver");
        if (heatSolveContactPairInputId.isValid() &&
            contactPairEmitterInputId.isValid() &&
            contactPairReceiverInputId.isValid()) {
            document.addConnection(receiverModelNode, receiverModelPtr->outputs[0].id, receiverRemeshNode, receiverRemeshPtr->inputs[0].id);
            document.addConnection(sourceModelNode, sourceModelPtr->outputs[0].id, sourceRemeshNode, sourceRemeshPtr->inputs[0].id);
            document.addConnection(receiverRemeshNode, receiverRemeshPtr->outputs[0].id, heatReceiverNode, heatReceiverPtr->inputs[0].id);
            document.addConnection(sourceRemeshNode, sourceRemeshPtr->outputs[0].id, heatSourceNode, heatSourcePtr->inputs[0].id);
            document.addConnection(heatSourceNode, heatSourcePtr->outputs[0].id, contactPairNode, contactPairEmitterInputId);
            document.addConnection(heatReceiverNode, heatReceiverPtr->outputs[0].id, contactPairNode, contactPairReceiverInputId);
            document.addConnection(contactPairNode, contactPairPtr->outputs[0].id, heatSolveNode, heatSolveContactPairInputId);
        }
    }

    NodeGraphParamValue receiverModelPath{};
    receiverModelPath.id = nodegraphparams::model::Path;
    receiverModelPath.type = NodeGraphParamType::String;
    receiverModelPath.stringValue = "models/channel_tube.obj";
    document.setNodeParameter(receiverModelNode, receiverModelPath);

    NodeGraphParamValue sourceModelPath{};
    sourceModelPath.id = nodegraphparams::model::Path;
    sourceModelPath.type = NodeGraphParamType::String;
    sourceModelPath.stringValue = "models/heatsource_tube.obj";
    document.setNodeParameter(sourceModelNode, sourceModelPath);

    ensureHeatSolveSpareInputLocked(heatSolveNode, NodeGraphValueType::ContactPair);

    rebuildStateLocked();

    std::vector<NodeGraphChange> changes;
    changes.push_back(NodeGraphChange{NodeGraphChangeType::Reset});
    changes.reserve(1 + graphState.nodes.size() + graphState.edges.size());
    for (const NodeGraphNode& node : graphState.nodes) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.node = node;
        changes.push_back(std::move(change));
    }
    for (const NodeGraphEdge& edge : graphState.edges) {
        NodeGraphChange change{NodeGraphChangeType::EdgeUpsert};
        change.edge = edge;
        changes.push_back(std::move(change));
    }
    pushChangesLocked(changes);
}

NodeGraphNodeId NodeGraphBridge::addNode(const NodeTypeId& typeId, const std::string& title, float x, float y) {
    std::lock_guard<std::mutex> lock(mutex);

    const NodeGraphNodeId nodeId = document.addNode(typeId, title, x, y);
    if (!nodeId.isValid()) {
        return {};
    }

    std::vector<NodeGraphChange> changes;
    if (const NodeGraphNode* node = document.findNode(nodeId)) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.node = *node;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return nodeId;
}

bool NodeGraphBridge::removeNode(NodeGraphNodeId nodeId) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!document.findNode(nodeId)) {
        return false;
    }
    if (!document.removeNode(nodeId)) {
        return false;
    }

    NodeGraphChange change{NodeGraphChangeType::NodeRemoved};
    change.nodeId = nodeId;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraphBridge::moveNode(NodeGraphNodeId nodeId, float x, float y) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!document.moveNode(nodeId, x, y)) {
        return false;
    }

    std::vector<NodeGraphChange> changes;
    if (const NodeGraphNode* node = document.findNode(nodeId)) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.node = *node;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraphBridge::getNode(NodeGraphNodeId nodeId, NodeGraphNode& outNode) const {
    std::lock_guard<std::mutex> lock(mutex);

    const NodeGraphNode* node = document.findNode(nodeId);
    if (!node) {
        return false;
    }

    outNode = *node;
    return true;
}

bool NodeGraphBridge::setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!document.setNodeParameter(nodeId, parameter)) {
        return false;
    }

    std::vector<NodeGraphChange> changes;
    if (const NodeGraphNode* node = document.findNode(nodeId)) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.node = *node;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraphBridge::ensureHeatSolveSpareInputLocked(NodeGraphNodeId nodeId, NodeGraphValueType valueType) {
    if (valueType != NodeGraphValueType::ContactPair &&
        valueType != NodeGraphValueType::HeatReceiver &&
        valueType != NodeGraphValueType::HeatSource) {
        return false;
    }

    const NodeGraphNode* node = document.findNode(nodeId);
    if (!node || canonicalNodeTypeId(node->typeId) != nodegraphtypes::HeatSolve) {
        return false;
    }

    NodeSocketSignature socketTemplate{};
    bool hasTemplate = false;
    std::size_t socketCount = 0;
    bool hasSpare = false;
    for (const NodeGraphSocket& inputSocket : node->inputs) {
        if (inputSocket.valueType != valueType) {
            continue;
        }

        ++socketCount;
        if (!hasTemplate) {
            socketTemplate.name = inputSocket.name;
            socketTemplate.direction = NodeGraphSocketDirection::Input;
            socketTemplate.valueType = inputSocket.valueType;
            socketTemplate.contract = inputSocket.contract;
            hasTemplate = true;
        }

        if (!findIncomingEdgeId(document, nodeId, inputSocket.id).isValid()) {
            hasSpare = true;
            break;
        }
    }

    if (!hasTemplate || hasSpare) {
        return false;
    }

    socketTemplate.name = makeHeatSolveSocketName(valueType, socketCount + 1);
    return document.appendSocket(nodeId, socketTemplate, nullptr);
}

bool NodeGraphBridge::connectSockets(
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    std::string& errorMessage,
    bool replaceExistingInput) {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<NodeGraphChange> changes;
    const NodeGraphSocket* targetInputSocket = document.findInputSocket(toNode, toSocket);
    const NodeGraphValueType targetInputValueType =
        targetInputSocket ? targetInputSocket->valueType : NodeGraphValueType::Unknown;

    NodeGraphEdgeId newEdgeId{};
    if (NodeGraphValidator::canCreateConnection(document, fromNode, fromSocket, toNode, toSocket, errorMessage)) {
        document.addConnection(fromNode, fromSocket, toNode, toSocket, &newEdgeId);
        if (const NodeGraphEdge* edge = findEdgeById(document, newEdgeId)) {
            NodeGraphChange edgeChange{NodeGraphChangeType::EdgeUpsert};
            edgeChange.edge = *edge;
            changes.push_back(std::move(edgeChange));
        }
        if (ensureHeatSolveSpareInputLocked(toNode, targetInputValueType)) {
            if (const NodeGraphNode* node = document.findNode(toNode)) {
                NodeGraphChange nodeChange{NodeGraphChangeType::NodeUpsert};
                nodeChange.node = *node;
                changes.push_back(std::move(nodeChange));
            }
        }
        rebuildStateLocked();
        pushChangesLocked(changes);
        return true;
    }

    if (!replaceExistingInput) {
        return false;
    }

    const NodeGraphEdgeId existingIncomingEdgeId = findIncomingEdgeId(document, toNode, toSocket);
    if (!existingIncomingEdgeId.isValid()) {
        return false;
    }

    if (!NodeGraphValidator::canCreateConnection(
            document,
            fromNode,
            fromSocket,
            toNode,
            toSocket,
            errorMessage,
            existingIncomingEdgeId)) {
        return false;
    }

    if (!document.removeConnection(existingIncomingEdgeId)) {
        errorMessage = "Failed to replace existing input connection.";
        return false;
    }
    NodeGraphChange removedChange{NodeGraphChangeType::EdgeRemoved};
    removedChange.edgeId = existingIncomingEdgeId;
    changes.push_back(std::move(removedChange));

    document.addConnection(fromNode, fromSocket, toNode, toSocket, &newEdgeId);
    if (const NodeGraphEdge* edge = findEdgeById(document, newEdgeId)) {
        NodeGraphChange edgeChange{NodeGraphChangeType::EdgeUpsert};
        edgeChange.edge = *edge;
        changes.push_back(std::move(edgeChange));
    }

    if (ensureHeatSolveSpareInputLocked(toNode, targetInputValueType)) {
        if (const NodeGraphNode* node = document.findNode(toNode)) {
            NodeGraphChange nodeChange{NodeGraphChangeType::NodeUpsert};
            nodeChange.node = *node;
            changes.push_back(std::move(nodeChange));
        }
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraphBridge::removeConnection(NodeGraphEdgeId edgeId) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!findEdgeById(document, edgeId)) {
        return false;
    }
    if (!document.removeConnection(edgeId)) {
        return false;
    }

    NodeGraphChange change{NodeGraphChangeType::EdgeRemoved};
    change.edgeId = edgeId;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

NodeGraphExecutionPlan NodeGraphBridge::executionPlan() const {
    std::lock_guard<std::mutex> lock(mutex);
    return NodeGraphExecutionPlanner::buildPlan(graphState);
}

bool NodeGraphBridge::canExecuteHeatSolve(std::string& reason) const {
    const NodeGraphExecutionPlan plan = executionPlan();
    reason = plan.heatSolveBlockReason;
    return plan.canExecuteHeatSolve;
}

NodeGraphState NodeGraphBridge::state() const {
    std::lock_guard<std::mutex> lock(mutex);
    return graphState;
}

bool NodeGraphBridge::consumeChanges(uint64_t& lastSeenRevision, NodeGraphDelta& outDelta) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (lastSeenRevision == changeRevision) {
        return false;
    }

    outDelta = {};
    outDelta.fromRevision = lastSeenRevision;
    outDelta.toRevision = changeRevision;
    for (const auto& entry : changeLog) {
        if (entry.first > lastSeenRevision) {
            outDelta.changes.push_back(entry.second);
        }
    }

    lastSeenRevision = changeRevision;
    return true;
}

void NodeGraphBridge::rebuildStateLocked() {
    graphState.nodes = document.getNodes();
    graphState.edges = document.getEdges();
}

void NodeGraphBridge::pushChangesLocked(const std::vector<NodeGraphChange>& changes) {
    if (changes.empty()) {
        return;
    }

    ++changeRevision;
    graphState.revision = changeRevision;
    for (const NodeGraphChange& change : changes) {
        changeLog.push_back({changeRevision, change});
    }
}
