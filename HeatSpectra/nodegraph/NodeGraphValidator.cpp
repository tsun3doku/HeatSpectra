#include "NodeGraphValidator.hpp"

#include "NodeGraphTypes.hpp"

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

bool NodeGraphValidator::canCreateConnection(
    const NodeGraphDocument& document,
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    std::string& errorMessage,
    NodeGraphEdgeId ignoreExistingEdge) {
    errorMessage.clear();

    if (!fromNode.isValid() || !toNode.isValid()) {
        errorMessage = "Connection uses an invalid node id.";
        return false;
    }

    if (fromNode == toNode) {
        errorMessage = "A node cannot connect to itself.";
        return false;
    }

    const NodeGraphNode* srcNode = document.findNode(fromNode);
    const NodeGraphNode* dstNode = document.findNode(toNode);
    if (!srcNode || !dstNode) {
        errorMessage = "Connection references a missing node.";
        return false;
    }

    const NodeGraphSocket* srcSocket = document.findOutputSocket(fromNode, fromSocket);
    const NodeGraphSocket* dstSocket = document.findInputSocket(toNode, toSocket);
    if (!srcSocket || !dstSocket) {
        errorMessage = "Connection references a missing input or output socket.";
        return false;
    }

    const NodePayloadType producedPayloadType = srcSocket->contract.producedPayloadType;
    if (!acceptsPayload(*dstSocket, producedPayloadType)) {
        errorMessage = "Data contract mismatch: output '" + srcSocket->name + "' provides '" +
            std::string(nodePayloadTypeName(producedPayloadType)) + "' but input '" + dstSocket->name +
            "' does not accept it.";
        return false;
    }

    const auto duplicateEdgeIt = std::find_if(document.getEdges().begin(), document.getEdges().end(), [&](const NodeGraphEdge& edge) {
        if (ignoreExistingEdge.isValid() && edge.id == ignoreExistingEdge) {
            return false;
        }
        return edge.fromNode == fromNode && edge.fromSocket == fromSocket && edge.toNode == toNode && edge.toSocket == toSocket;
    });
    if (duplicateEdgeIt != document.getEdges().end()) {
        errorMessage = "Connection already exists.";
        return false;
    }

    const auto inputAlreadyConnectedIt = std::find_if(document.getEdges().begin(), document.getEdges().end(), [&](const NodeGraphEdge& edge) {
        if (ignoreExistingEdge.isValid() && edge.id == ignoreExistingEdge) {
            return false;
        }
        return edge.toNode == toNode && edge.toSocket == toSocket;
    });
    if (inputAlreadyConnectedIt != document.getEdges().end()) {
        errorMessage = "Input socket already has a connection.";
        return false;
    }

    if (wouldIntroduceCycle(document, fromNode, toNode)) {
        errorMessage = "Connection would introduce a cycle.";
        return false;
    }

    return true;
}

bool NodeGraphValidator::wouldIntroduceCycle(
    const NodeGraphDocument& document,
    NodeGraphNodeId fromNode,
    NodeGraphNodeId toNode) {
    std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const NodeGraphEdge& edge : document.getEdges()) {
        adjacency[edge.fromNode.value].push_back(edge.toNode.value);
    }

    std::queue<uint32_t> pending;
    std::unordered_set<uint32_t> visited;
    pending.push(toNode.value);

    while (!pending.empty()) {
        const uint32_t node = pending.front();
        pending.pop();

        if (!visited.insert(node).second) {
            continue;
        }

        if (node == fromNode.value) {
            return true;
        }

        const auto adjIt = adjacency.find(node);
        if (adjIt == adjacency.end()) {
            continue;
        }

        for (uint32_t next : adjIt->second) {
            pending.push(next);
        }
    }

    return false;
}
