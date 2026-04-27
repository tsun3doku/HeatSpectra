#include "NodeGraphBridge.hpp"

#include "NodeGraphRegistry.hpp"
#include "NodeGraphTopology.hpp"
#include "NodeGraphValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
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

}

NodeGraphBridge::NodeGraphBridge() {
    rebuildStateLocked();
}

void NodeGraphBridge::clear() {
    std::lock_guard<std::mutex> lock(mutex);

    document.clear();

    rebuildStateLocked();

    std::vector<NodeGraphChange> changes;
    NodeGraphChange resetChange{NodeGraphChangeType::Reset};
    resetChange.reason = NodeGraphChangeReason::Topology;
    changes.push_back(std::move(resetChange));
    changes.reserve(1 + graphState.nodes.size() + graphState.edges.size());
    for (const NodeGraphNode& node : graphState.nodes) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.reason = NodeGraphChangeReason::Topology;
        change.node = node;
        changes.push_back(std::move(change));
    }
    for (const NodeGraphEdge& edge : graphState.edges) {
        NodeGraphChange change{NodeGraphChangeType::EdgeUpsert};
        change.reason = NodeGraphChangeReason::Topology;
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
        change.reason = NodeGraphChangeReason::Topology;
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
    change.reason = NodeGraphChangeReason::Topology;
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
        change.reason = NodeGraphChangeReason::Layout;
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

bool NodeGraphBridge::setNodeDisplayEnabled(NodeGraphNodeId nodeId, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);

    std::unordered_map<uint32_t, std::pair<bool, bool>> stateByNodeId;
    stateByNodeId.reserve(document.getNodes().size());
    for (const NodeGraphNode& existingNode : document.getNodes()) {
        stateByNodeId.emplace(existingNode.id.value, std::make_pair(existingNode.displayEnabled, existingNode.frozen));
    }

    if (!document.setNodeDisplayEnabled(nodeId, enabled)) {
        return false;
    }

    std::vector<NodeGraphChange> changes;
    for (const NodeGraphNode& updatedNode : document.getNodes()) {
        const auto previousIt = stateByNodeId.find(updatedNode.id.value);
        const bool previousDisplayEnabled = previousIt != stateByNodeId.end() ? previousIt->second.first : false;
        const bool previousFrozen = previousIt != stateByNodeId.end() ? previousIt->second.second : false;
        if (previousDisplayEnabled == updatedNode.displayEnabled && previousFrozen == updatedNode.frozen) {
            continue;
        }

        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.reason = NodeGraphChangeReason::State;
        change.node = updatedNode;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraphBridge::setNodeFrozen(NodeGraphNodeId nodeId, bool frozen) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!document.setNodeFrozen(nodeId, frozen)) {
        return false;
    }

    std::vector<NodeGraphChange> changes;
    if (const NodeGraphNode* node = document.findNode(nodeId)) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.reason = NodeGraphChangeReason::State;
        change.node = *node;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
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
        change.reason = NodeGraphChangeReason::Parameter;
        change.node = *node;
        changes.push_back(std::move(change));
    }

    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
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

    NodeGraphEdgeId newEdgeId{};
    if (NodeGraphValidator::canCreateConnection(document, fromNode, fromSocket, toNode, toSocket, errorMessage)) {
        document.addConnection(fromNode, fromSocket, toNode, toSocket, &newEdgeId);
        if (const NodeGraphEdge* edge = findEdgeById(document, newEdgeId)) {
            NodeGraphChange edgeChange{NodeGraphChangeType::EdgeUpsert};
            edgeChange.reason = NodeGraphChangeReason::Topology;
            edgeChange.edge = *edge;
            changes.push_back(std::move(edgeChange));
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

    if (!NodeGraphValidator::canCreateConnection(document, fromNode, fromSocket, toNode, toSocket, errorMessage, existingIncomingEdgeId)) {
        return false;
    }

    if (!document.removeConnection(existingIncomingEdgeId)) {
        errorMessage = "Failed to replace existing input connection.";
        return false;
    }
    NodeGraphChange removedChange{NodeGraphChangeType::EdgeRemoved};
    removedChange.reason = NodeGraphChangeReason::Topology;
    removedChange.edgeId = existingIncomingEdgeId;
    changes.push_back(std::move(removedChange));

    document.addConnection(fromNode, fromSocket, toNode, toSocket, &newEdgeId);
    if (const NodeGraphEdge* edge = findEdgeById(document, newEdgeId)) {
        NodeGraphChange edgeChange{NodeGraphChangeType::EdgeUpsert};
        edgeChange.reason = NodeGraphChangeReason::Topology;
        edgeChange.edge = *edge;
        changes.push_back(std::move(edgeChange));
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
    change.reason = NodeGraphChangeReason::Topology;
    change.edgeId = edgeId;
    std::vector<NodeGraphChange> changes;
    changes.push_back(std::move(change));
    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraphBridge::canExecuteHeatSolve(std::string& reason) const {
    std::lock_guard<std::mutex> lock(mutex);
    const NodeGraphCompiled plan = NodeGraphCompiler::compile(graphState);
    if (!plan.isValid) {
        if (!plan.compilationErrors.empty()) {
            reason = plan.compilationErrors.front();
        } else {
            reason = "Graph contains compilation errors.";
        }
    }
    return plan.isValid;
}

NodeGraphState NodeGraphBridge::state() const {
    std::lock_guard<std::mutex> lock(mutex);
    return graphState;
}

bool NodeGraphBridge::resolveGizmoTransformNode(uint64_t outputSocketKey, NodeGraphNodeId& outTransformNodeId) const {
    outTransformNodeId = {};
    std::lock_guard<std::mutex> lock(mutex);
    if (outputSocketKey == 0) {
        return false;
    }

    const NodeGraphTopology topology(graphState);
    return topology.findFirstUpstreamNodeByType(outputSocketKey, nodegraphtypes::Transform, outTransformNodeId);
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
