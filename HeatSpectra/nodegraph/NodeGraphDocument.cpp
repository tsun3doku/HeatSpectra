#include "NodeGraphDocument.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

NodeGraphDocument::NodeGraphDocument() = default;

NodeGraphNodeId NodeGraphDocument::addNode(const NodeTypeId& typeId, const std::string& title, float x, float y) {
    const NodeTypeId canonicalTypeId = canonicalNodeTypeId(typeId);
    const NodeTypeDefinition* definition = findNodeTypeDefinitionById(canonicalTypeId);
    if (!definition) {
        return {};
    }

    NodeGraphNode node{};
    node.id = NodeGraphNodeId{nextNodeId++};
    node.typeId = definition->id;
    node.category = definition->category;
    node.title = title.empty() ? definition->displayName : title;
    node.x = x;
    node.y = y;
    node.inputs = buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Input);
    node.outputs = buildSocketsFromInterface(*definition, NodeGraphSocketDirection::Output);
    for (const NodeGraphParamDefinition& parameter : definition->parameters) {
        node.parameters.push_back(makeNodeGraphParamValue(parameter));
    }

    nodes.push_back(std::move(node));
    bumpRevision();
    return nodes.back().id;
}

bool NodeGraphDocument::removeNode(NodeGraphNodeId nodeId) {
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [nodeId](const NodeGraphNode& node) {
        return node.id == nodeId;
    });

    if (nodeIt == nodes.end()) {
        return false;
    }

    nodes.erase(nodeIt);
    edges.erase(
        std::remove_if(edges.begin(), edges.end(), [nodeId](const NodeGraphEdge& edge) {
            return edge.fromNode == nodeId || edge.toNode == nodeId;
        }),
        edges.end());
    bumpRevision();
    return true;
}

bool NodeGraphDocument::moveNode(NodeGraphNodeId nodeId, float x, float y) {
    NodeGraphNode* node = findNode(nodeId);
    if (!node) {
        return false;
    }

    if (std::fabs(node->x - x) < 0.01f && std::fabs(node->y - y) < 0.01f) {
        return false;
    }

    node->x = x;
    node->y = y;
    bumpRevision();
    return true;
}

bool NodeGraphDocument::setNodeParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    NodeGraphNode* node = findNode(nodeId);
    if (!node) {
        return false;
    }

    const NodeTypeDefinition* definition = findNodeTypeDefinitionById(node->typeId);
    if (!definition) {
        return false;
    }

    const NodeGraphParamDefinition* parameterDefinition = findNodeParamDefinition(*definition, parameter.id);
    if (!parameterDefinition || parameterDefinition->type != parameter.type) {
        return false;
    }

    NodeGraphParamValue* existingParameter = findNodeParamValue(*node, parameter.id);
    if (existingParameter) {
        *existingParameter = parameter;
    } else {
        node->parameters.push_back(parameter);
    }

    bumpRevision();
    return true;
}

bool NodeGraphDocument::appendSocket(
    NodeGraphNodeId nodeId,
    const NodeSocketSignature& socketSignature,
    NodeGraphSocketId* outSocketId) {
    NodeGraphNode* node = findNode(nodeId);
    if (!node) {
        return false;
    }

    NodeGraphSocket socket{};
    socket.id = allocateSocketId();
    socket.name = socketSignature.name;
    socket.valueType = socketSignature.valueType;
    socket.direction = socketSignature.direction;
    socket.contract = socketSignature.contract;

    if (socket.direction == NodeGraphSocketDirection::Input) {
        node->inputs.push_back(socket);
    } else {
        node->outputs.push_back(socket);
    }

    if (outSocketId) {
        *outSocketId = socket.id;
    }

    bumpRevision();
    return true;
}

bool NodeGraphDocument::addConnection(
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    NodeGraphEdgeId* outEdgeId) {
    NodeGraphEdge edge{};
    edge.id = NodeGraphEdgeId{nextEdgeId++};
    edge.fromNode = fromNode;
    edge.fromSocket = fromSocket;
    edge.toNode = toNode;
    edge.toSocket = toSocket;

    edges.push_back(edge);
    bumpRevision();

    if (outEdgeId) {
        *outEdgeId = edge.id;
    }
    return true;
}

bool NodeGraphDocument::removeConnection(NodeGraphEdgeId edgeId) {
    const auto edgeIt = std::find_if(edges.begin(), edges.end(), [edgeId](const NodeGraphEdge& edge) {
        return edge.id == edgeId;
    });

    if (edgeIt == edges.end()) {
        return false;
    }

    edges.erase(edgeIt);
    bumpRevision();
    return true;
}

void NodeGraphDocument::clear() {
    nodes.clear();
    edges.clear();
    nextNodeId = 1;
    nextSocketId = 1;
    nextEdgeId = 1;
    bumpRevision();
}

const NodeGraphNode* NodeGraphDocument::findNode(NodeGraphNodeId nodeId) const {
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [nodeId](const NodeGraphNode& node) {
        return node.id == nodeId;
    });

    return nodeIt != nodes.end() ? &(*nodeIt) : nullptr;
}

NodeGraphNode* NodeGraphDocument::findNode(NodeGraphNodeId nodeId) {
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [nodeId](const NodeGraphNode& node) {
        return node.id == nodeId;
    });

    return nodeIt != nodes.end() ? &(*nodeIt) : nullptr;
}

const NodeGraphSocket* NodeGraphDocument::findInputSocket(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) const {
    const NodeGraphNode* node = findNode(nodeId);
    if (!node) {
        return nullptr;
    }

    const auto socketIt = std::find_if(node->inputs.begin(), node->inputs.end(), [socketId](const NodeGraphSocket& socket) {
        return socket.id == socketId;
    });
    return socketIt != node->inputs.end() ? &(*socketIt) : nullptr;
}

const NodeGraphSocket* NodeGraphDocument::findOutputSocket(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) const {
    const NodeGraphNode* node = findNode(nodeId);
    if (!node) {
        return nullptr;
    }

    const auto socketIt = std::find_if(node->outputs.begin(), node->outputs.end(), [socketId](const NodeGraphSocket& socket) {
        return socket.id == socketId;
    });
    return socketIt != node->outputs.end() ? &(*socketIt) : nullptr;
}

NodeGraphSocketId NodeGraphDocument::allocateSocketId() {
    return NodeGraphSocketId{nextSocketId++};
}

std::vector<NodeGraphSocket> NodeGraphDocument::buildSocketsFromInterface(
    const NodeTypeDefinition& definition,
    NodeGraphSocketDirection direction) {
    std::vector<NodeGraphSocket> sockets;
    for (const NodeSocketSignature& socketSignature : definition.sockets) {
        if (socketSignature.direction != direction) {
            continue;
        }

        sockets.push_back(
            {
                allocateSocketId(),
                socketSignature.name,
                socketSignature.valueType,
                socketSignature.direction,
                socketSignature.contract,
            });
    }

    return sockets;
}

void NodeGraphDocument::bumpRevision() {
    ++revision;
}
