#include "NodeGraph.hpp"
#include "NodeGraphInit.hpp"
#include "NodeGraphLayout.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphValidator.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>

NodeGraph::NodeGraph() {
    initNodeGraph(registry);
    rebuildStateLocked();
}

static void copySocketIdsByIndex(
    std::vector<NodeGraphSocket>& rebuiltSockets,
    const std::vector<NodeGraphSocket>& savedSockets) {
    const std::size_t count = std::min(rebuiltSockets.size(), savedSockets.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (savedSockets[i].id.isValid()) {
            rebuiltSockets[i].id = savedSockets[i].id;
        }
    }
}

static uint32_t maxSocketId(const NodeGraphNode& node) {
    uint32_t maxId = 0;
    for (const NodeGraphSocket& socket : node.inputs) {
        maxId = std::max(maxId, socket.id.value);
    }
    for (const NodeGraphSocket& socket : node.outputs) {
        maxId = std::max(maxId, socket.id.value);
    }
    return maxId;
}
NodeGraphNodeId NodeGraph::addNode(const NodeTypeId& typeId, const std::string& title, float x, float y) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const NodeTypeDefinition* definition = registry.findNodeType(typeId);
    if (!definition) {
        return {};
    }

    NodeGraphNode node{};
    node.id = NodeGraphNodeId{nextNodeId++};
    node.typeId = definition->id;
    node.category = definition->category;
    node.title = title.empty() ? definition->displayName : title;
    node.x = static_cast<float>(nodegraphlayout::snapCoordinate(x));
    node.y = static_cast<float>(nodegraphlayout::snapCoordinate(y));
    node.inputs = buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Input);
    node.outputs = buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Output);
    for (const NodeGraphParamDefinition& parameter : definition->parameters) {
        node.parameters.push_back(makeNodeGraphParamValue(parameter));
    }

    NodeGraphNodeId id = node.id;
    nodes[id.value] = std::move(node);
    bumpRevision();

    NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
    change.reason = NodeGraphChangeReason::Topology;
    change.node = nodes.at(id.value);
    rebuildStateLocked();
    pushChangesLocked({change});
    return id;
}

bool NodeGraph::removeNode(NodeGraphNodeId nodeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    auto it = nodes.find(nodeId.value);
    if (it == nodes.end()) {
        return false;
    }

    nodes.erase(it);
    std::vector<NodeGraphEdgeId> removedEdgeIds;
    for (const auto& [edgeKey, edge] : edges) {
        if (edge.fromNode == nodeId || edge.toNode == nodeId) {
            removedEdgeIds.push_back(edge.id);
        }
    }
    for (NodeGraphEdgeId edgeId : removedEdgeIds) {
        edges.remove(edgeId);
    }
    bumpRevision();

    NodeGraphChange change{NodeGraphChangeType::NodeRemoved};
    change.reason = NodeGraphChangeReason::Topology;
    change.nodeId = nodeId;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::moveNode(NodeGraphNodeId nodeId, float x, float y) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    NodeGraphNode* node = findNodeUnlocked(nodeId);
    if (!node) {
        return false;
    }

    const float snappedX = static_cast<float>(nodegraphlayout::snapCoordinate(x));
    const float snappedY = static_cast<float>(nodegraphlayout::snapCoordinate(y));

    if (std::fabs(node->x - snappedX) < 0.01f && std::fabs(node->y - snappedY) < 0.01f) {
        return false;
    }

    node->x = snappedX;
    node->y = snappedY;
    bumpRevision();

    NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
    change.reason = NodeGraphChangeReason::Layout;
    change.node = *node;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::getNode(NodeGraphNodeId nodeId, NodeGraphNode& outNode) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const NodeGraphNode* node = findNodeUnlocked(nodeId);
    if (!node) {
        return false;
    }

    outNode = *node;
    return true;
}

bool NodeGraph::toggleNodeFrozen(NodeGraphNodeId nodeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (!NodeGraphNodeState::toggleFrozen(nodes, nodeId)) {
        return false;
    }

    bumpRevision();
    NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
    change.reason = NodeGraphChangeReason::State;
    change.node = nodes.at(nodeId.value);
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::toggleNodeDisplay(NodeGraphNodeId nodeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (!NodeGraphNodeState::toggleDisplay(nodes, nodeId)) {
        return false;
    }

    bumpRevision();
    std::vector<NodeGraphChange> changes;
    for (const auto& [id, node] : nodes) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.reason = NodeGraphChangeReason::State;
        change.node = node;
        changes.push_back(std::move(change));
    }
    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraph::setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    return setNodeParameters(nodeId, std::vector<NodeGraphParamValue>{parameter});
}

bool NodeGraph::setNodeParameters(NodeGraphNodeId nodeId, const std::vector<NodeGraphParamValue>& parameters) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    NodeGraphNode* node = findNodeUnlocked(nodeId);
    if (!node || parameters.empty()) {
        return false;
    }

    const NodeTypeDefinition* definition = registry.findNodeType(node->typeId);
    if (!definition) {
        return false;
    }

    std::unordered_set<uint32_t> parameterIds;
    std::vector<NodeGraphParamValue> normalizedParameters;
    parameterIds.reserve(parameters.size());
    normalizedParameters.reserve(parameters.size());

    for (const NodeGraphParamValue& parameter : parameters) {
        if (!parameterIds.insert(parameter.id).second) {
            return false;
        }

        const NodeGraphParamDefinition* parameterDefinition = findNodeParamDefinition(*definition, parameter.id);
        NodeGraphParamValue normalizedParameter = parameter;
        if (!parameterDefinition ||
            !normalizeNodeGraphParamValue(*parameterDefinition, normalizedParameter) ||
            !validateNodeGraphParamValue(*parameterDefinition, normalizedParameter)) {
            return false;
        }
        normalizedParameters.push_back(std::move(normalizedParameter));
    }

    std::vector<NodeGraphParamValue> updatedParameters = node->parameters;
    for (NodeGraphParamValue& parameter : normalizedParameters) {
        const auto existingParameter = std::find_if(
            updatedParameters.begin(),
            updatedParameters.end(),
            [&parameter](const NodeGraphParamValue& candidate) {
                return candidate.id == parameter.id;
            });
        if (existingParameter != updatedParameters.end()) {
            *existingParameter = std::move(parameter);
        } else {
            updatedParameters.push_back(std::move(parameter));
        }
    }
    node->parameters.swap(updatedParameters);

    bumpRevision();
    NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
    change.reason = NodeGraphChangeReason::Parameter;
    change.node = *node;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::appendSocket(NodeGraphNodeId nodeId, const NodeSocketSignature& socketSignature, NodeGraphSocketId* outSocketId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    NodeGraphNode* node = findNodeUnlocked(nodeId);
    if (!node) {
        return false;
    }

    NodeGraphSocket socket(allocateSocketId(), socketSignature);
    if (socket.direction == NodeGraphSocketDirection::Input) {
        node->inputs.push_back(socket);
    } else {
        node->outputs.push_back(socket);
    }

    if (outSocketId) {
        *outSocketId = socket.id;
    }

    bumpRevision();
    NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
    change.reason = NodeGraphChangeReason::Topology;
    change.node = *node;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::addConnection(
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    NodeGraphEdgeId* outEdgeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    NodeGraphEdge edge{};
    edge.id = NodeGraphEdgeId{nextEdgeId++};
    edge.fromNode = fromNode;
    edge.fromSocket = fromSocket;
    edge.toNode = toNode;
    edge.toSocket = toSocket;

    edges.upsert(edge);
    bumpRevision();

    if (outEdgeId) {
        *outEdgeId = edge.id;
    }
    NodeGraphChange change{NodeGraphChangeType::EdgeUpsert};
    change.reason = NodeGraphChangeReason::Topology;
    change.edge = edge;
    change.edgeId = edge.id;
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

bool NodeGraph::connectSockets(
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    std::string& errorMessage,
    bool replaceExistingInput) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const NodeGraphNode* toNodePtr = findNodeUnlocked(toNode);
    const NodeGraphSocket* targetSocket = toNodePtr ? toNodePtr->input(toSocket) : nullptr;

    NodeGraphEdgeId ignoreEdgeId{};
    const NodeGraphEdge* existingEdge = findIncomingEdgeUnlocked(toNode, toSocket);
    if (existingEdge && targetSocket && !targetSocket->variadic) {
        if (!replaceExistingInput) {
            errorMessage = "Input socket already has a connection.";
            return false;
        }
        ignoreEdgeId = existingEdge->id;
    }

    if (!NodeGraphValidator::canCreateConnection(*this, fromNode, fromSocket, toNode, toSocket, errorMessage, ignoreEdgeId)) {
        return false;
    }

    std::vector<NodeGraphChange> changes;
    if (ignoreEdgeId.isValid()) {
        const NodeGraphEdge* ignoredEdge = edges.find(ignoreEdgeId);
        if (!ignoredEdge) {
            errorMessage = "Failed to replace existing input connection.";
            return false;
        }

        NodeGraphChange removedChange{NodeGraphChangeType::EdgeRemoved};
        removedChange.reason = NodeGraphChangeReason::Topology;
        removedChange.edge = *ignoredEdge;
        removedChange.edgeId = ignoreEdgeId;
        changes.push_back(std::move(removedChange));
        edges.remove(ignoreEdgeId);
    }

    NodeGraphEdge edge{};
    edge.id = NodeGraphEdgeId{nextEdgeId++};
    edge.fromNode = fromNode;
    edge.fromSocket = fromSocket;
    edge.toNode = toNode;
    edge.toSocket = toSocket;
    edges.upsert(edge);

    NodeGraphChange addedChange{NodeGraphChangeType::EdgeUpsert};
    addedChange.reason = NodeGraphChangeReason::Topology;
    addedChange.edge = edge;
    addedChange.edgeId = edge.id;
    changes.push_back(std::move(addedChange));

    bumpRevision();
    rebuildStateLocked();
    pushChangesLocked(changes);
    return true;
}

bool NodeGraph::removeConnection(NodeGraphEdgeId edgeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const NodeGraphEdge* existingEdge = edges.find(edgeId);
    if (!existingEdge) {
        return false;
    }

    NodeGraphChange change{NodeGraphChangeType::EdgeRemoved};
    change.reason = NodeGraphChangeReason::Topology;
    change.edge = *existingEdge;
    change.edgeId = edgeId;
    edges.remove(edgeId);
    bumpRevision();
    rebuildStateLocked();
    pushChangesLocked({change});
    return true;
}

void NodeGraph::clear() {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    nodes.clear();
    edges.clear();
    nextNodeId = 1;
    nextSocketId = 1;
    nextEdgeId = 1;
    bumpRevision();

    NodeGraphChange resetChange{NodeGraphChangeType::Reset};
    resetChange.reason = NodeGraphChangeReason::Topology;
    rebuildStateLocked();
    pushChangesLocked({resetChange});
}

bool NodeGraph::loadSerializedState(
    const NodeGraphState& state,
    uint32_t serializedNextNodeId,
    uint32_t serializedNextSocketId,
    uint32_t serializedNextEdgeId,
    std::string& errorMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    NodeGraph candidate;

    uint32_t maxNodeId = 0;
    uint32_t maxRestoredSocketId = 0;
    uint32_t maxEdgeId = 0;

    for (const auto& [nodeKey, savedNode] : state.nodes) {
        if (!savedNode.id.isValid() || nodeKey != savedNode.id.value) {
            errorMessage = "Saved node has an invalid id.";
            return false;
        }

        const NodeTypeDefinition* definition = registry.findNodeType(savedNode.typeId);
        if (!definition) {
            errorMessage = "Saved graph references unknown node type: " + savedNode.typeId;
            return false;
        }

        NodeGraphNode rebuiltNode{};
        rebuiltNode.id = savedNode.id;
        rebuiltNode.typeId = definition->id;
        rebuiltNode.category = definition->category;
        rebuiltNode.title = savedNode.title.empty() ? definition->displayName : savedNode.title;
        rebuiltNode.x = static_cast<float>(nodegraphlayout::snapCoordinate(savedNode.x));
        rebuiltNode.y = static_cast<float>(nodegraphlayout::snapCoordinate(savedNode.y));
        rebuiltNode.state = savedNode.state;
        rebuiltNode.inputs = candidate.buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Input);
        rebuiltNode.outputs = candidate.buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Output);

        copySocketIdsByIndex(rebuiltNode.inputs, savedNode.inputs);
        copySocketIdsByIndex(rebuiltNode.outputs, savedNode.outputs);

        for (const NodeGraphParamDefinition& parameterDefinition : definition->parameters) {
            rebuiltNode.parameters.push_back(makeNodeGraphParamValue(parameterDefinition));
        }

        for (const NodeGraphParamValue& savedParameter : savedNode.parameters) {
            const NodeGraphParamDefinition* parameterDefinition = findNodeParamDefinition(*definition, savedParameter.id);
            if (!parameterDefinition) {
                continue;
            }

            NodeGraphParamValue restoredParameter = savedParameter;
            if (!normalizeNodeGraphParamValue(*parameterDefinition, restoredParameter) ||
                !validateNodeGraphParamValue(*parameterDefinition, restoredParameter)) {
                std::ostringstream ss;
                ss << "Node " << savedNode.id.value << " has an invalid saved parameter value.";
                errorMessage = ss.str();
                return false;
            }

            if (NodeGraphParamValue* existingParameter = findNodeParamValue(rebuiltNode, restoredParameter.id)) {
                *existingParameter = std::move(restoredParameter);
            } else {
                rebuiltNode.parameters.push_back(std::move(restoredParameter));
            }
        }

        maxNodeId = std::max(maxNodeId, rebuiltNode.id.value);
        maxRestoredSocketId = std::max(maxRestoredSocketId, maxSocketId(rebuiltNode));
        candidate.nodes[rebuiltNode.id.value] = std::move(rebuiltNode);
    }

    for (const auto& [edgeKey, savedEdge] : state.edges) {
        if (!savedEdge.id.isValid() || edgeKey != savedEdge.id.value) {
            errorMessage = "Saved edge has an invalid id.";
            return false;
        }

        const NodeGraphNode* fromNode = candidate.findNode(savedEdge.fromNode);
        const NodeGraphNode* toNode = candidate.findNode(savedEdge.toNode);
        if (!fromNode || !toNode) {
            errorMessage = "Saved edge references a missing node.";
            return false;
        }
        if (!fromNode->output(savedEdge.fromSocket) ||
            !toNode->input(savedEdge.toSocket)) {
            errorMessage = "Saved edge references a missing or invalid socket.";
            return false;
        }

        std::string validationError;
        if (!NodeGraphValidator::canCreateConnection(
                candidate,
                savedEdge.fromNode,
                savedEdge.fromSocket,
                savedEdge.toNode,
                savedEdge.toSocket,
                validationError)) {
            errorMessage = "Saved edge is invalid: " + validationError;
            return false;
        }

        candidate.edges.upsert(savedEdge);
        maxEdgeId = std::max(maxEdgeId, savedEdge.id.value);
    }

    candidate.nextNodeId = std::max(serializedNextNodeId, maxNodeId + 1);
    candidate.nextSocketId = std::max(serializedNextSocketId, maxRestoredSocketId + 1);
    candidate.nextEdgeId = std::max(serializedNextEdgeId, maxEdgeId + 1);
    candidate.revision = revision + 1;

    nodes = std::move(candidate.nodes);
    edges = std::move(candidate.edges);
    nextNodeId = candidate.nextNodeId;
    nextSocketId = candidate.nextSocketId;
    nextEdgeId = candidate.nextEdgeId;
    revision = candidate.revision;

    rebuildStateLocked();
    std::vector<NodeGraphChange> changes;
    changes.reserve(1 + graphState.nodes.size() + graphState.edges.size());
    NodeGraphChange resetChange{NodeGraphChangeType::Reset};
    resetChange.reason = NodeGraphChangeReason::Topology;
    changes.push_back(std::move(resetChange));
    for (const auto& [id, node] : graphState.nodes) {
        NodeGraphChange change{NodeGraphChangeType::NodeUpsert};
        change.reason = NodeGraphChangeReason::Topology;
        change.node = node;
        changes.push_back(std::move(change));
    }
    for (const auto& [id, edge] : graphState.edges) {
        NodeGraphChange change{NodeGraphChangeType::EdgeUpsert};
        change.reason = NodeGraphChangeReason::Topology;
        change.edge = edge;
        changes.push_back(std::move(change));
    }
    pushChangesLocked(changes);
    return true;
}

void NodeGraph::getNextIds(uint32_t& outNodeId, uint32_t& outSocketId, uint32_t& outEdgeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    outNodeId = nextNodeId;
    outSocketId = nextSocketId;
    outEdgeId = nextEdgeId;
}

bool NodeGraph::canExecute(std::string& reason) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);

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

NodeGraphState NodeGraph::state() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return graphState;
}

uint64_t NodeGraph::getRevision() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return revision;
}

bool NodeGraph::resolveGizmoTransformNode(uint64_t outputSocketKey, NodeGraphNodeId& outTransformNodeId) const {
    outTransformNodeId = {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (outputSocketKey == 0) {
        return false;
    }

    return findFirstUpstreamNodeByType(graphState, outputSocketKey, nodegraphtypes::Transform, outTransformNodeId);
}

bool NodeGraph::consumeChanges(uint64_t& lastSeenRevision, NodeGraphDelta& outDelta) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (lastSeenRevision == revision) {
        return false;
    }

    outDelta = {};
    outDelta.fromRevision = lastSeenRevision;
    outDelta.toRevision = revision;
    for (const auto& entry : changeLog) {
        if (entry.first > lastSeenRevision) {
            outDelta.changes.push_back(entry.second);
        }
    }

    lastSeenRevision = revision;
    return true;
}

std::unordered_map<uint32_t, NodeGraphNode> NodeGraph::getNodes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return nodes;
}

std::unordered_map<uint32_t, NodeGraphEdge> NodeGraph::getEdges() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return edges.toMap();
}

const NodeGraphNode* NodeGraph::findNode(NodeGraphNodeId nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return findNodeUnlocked(nodeId);
}

NodeGraphNode* NodeGraph::findNode(NodeGraphNodeId nodeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return findNodeUnlocked(nodeId);
}

const NodeGraphEdge* NodeGraph::findEdge(NodeGraphEdgeId edgeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return findEdgeUnlocked(edgeId);
}

const NodeGraphEdge* NodeGraph::findIncomingEdge(NodeGraphNodeId toNode, NodeGraphSocketId toSocket) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return findIncomingEdgeUnlocked(toNode, toSocket);
}

const NodeGraphNode* NodeGraph::findNodeUnlocked(NodeGraphNodeId nodeId) const {
    auto it = nodes.find(nodeId.value);
    return it != nodes.end() ? &it->second : nullptr;
}

NodeGraphNode* NodeGraph::findNodeUnlocked(NodeGraphNodeId nodeId) {
    auto it = nodes.find(nodeId.value);
    return it != nodes.end() ? &it->second : nullptr;
}

const NodeGraphEdge* NodeGraph::findEdgeUnlocked(NodeGraphEdgeId edgeId) const {
    return edges.find(edgeId);
}

const NodeGraphEdge* NodeGraph::findIncomingEdgeUnlocked(NodeGraphNodeId toNode, NodeGraphSocketId toSocket) const {
    return edges.incomingEdge(toNode, toSocket);
}

NodeGraphSocketId NodeGraph::allocateSocketId() {
    return NodeGraphSocketId{nextSocketId++};
}

std::vector<NodeGraphSocket> NodeGraph::buildSocketsFromInterface(
    const NodeTypeDefinition& definition,
    NodeGraphSocketDirection direction) {
    std::vector<NodeGraphSocket> sockets;
    for (const NodeSocketSignature& socketSignature : definition.sockets) {
        if (socketSignature.direction != direction) {
            continue;
        }

        sockets.emplace_back(allocateSocketId(), socketSignature);
    }

    return sockets;
}

void NodeGraph::bumpRevision() {
    ++revision;
}

void NodeGraph::rebuildStateLocked() {
    graphState.nodes = nodes;
    graphState.edges = edges;
    graphState.revision = revision;
    graphState.nextNodeId = nextNodeId;
    graphState.nextSocketId = nextSocketId;
    graphState.nextEdgeId = nextEdgeId;
}

void NodeGraph::pushChangesLocked(const std::vector<NodeGraphChange>& changes) {
    if (changes.empty()) {
        return;
    }

    graphState.revision = revision;
    for (const NodeGraphChange& change : changes) {
        changeLog.push_back({revision, change});
    }
}
