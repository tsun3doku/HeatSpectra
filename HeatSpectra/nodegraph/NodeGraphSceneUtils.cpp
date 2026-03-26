#include "NodeGraphSceneUtils.hpp"

namespace nodegraphsceneutils {

NodeGraphEdgeId findIncomingEdgeForInput(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    NodeGraphSocketId socketId) {
    for (const NodeGraphEdge& edge : state.edges) {
        if (edge.toNode == nodeId && edge.toSocket == socketId) {
            return edge.id;
        }
    }

    return {};
}

const NodeGraphNode* findStateNodeById(const NodeGraphState& state, NodeGraphNodeId nodeId) {
    for (const NodeGraphNode& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

int findSocketIndexById(const std::vector<NodeGraphSocket>& sockets, NodeGraphSocketId socketId) {
    for (std::size_t index = 0; index < sockets.size(); ++index) {
        if (sockets[index].id == socketId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

NodeGraphSocketId socketByIndex(const std::vector<NodeGraphSocket>& sockets, int index) {
    if (index < 0 || index >= static_cast<int>(sockets.size())) {
        return {};
    }
    return sockets[static_cast<std::size_t>(index)].id;
}

std::vector<NodeGraphSocketId> matchingInputSocketsByType(
    const NodeGraphNode& node,
    NodeGraphValueType valueType) {
    std::vector<NodeGraphSocketId> sockets;
    for (const NodeGraphSocket& socket : node.inputs) {
        if (socket.valueType == valueType) {
            sockets.push_back(socket.id);
        }
    }
    return sockets;
}

int valueTypeOrdinalAtInputIndex(const NodeGraphNode& node, int inputIndex) {
    if (inputIndex < 0 || inputIndex >= static_cast<int>(node.inputs.size())) {
        return -1;
    }

    const NodeGraphValueType valueType = node.inputs[static_cast<std::size_t>(inputIndex)].valueType;
    int ordinal = 0;
    for (int index = 0; index <= inputIndex; ++index) {
        if (node.inputs[static_cast<std::size_t>(index)].valueType == valueType) {
            ++ordinal;
        }
    }
    return ordinal - 1;
}

} // namespace nodegraphsceneutils
