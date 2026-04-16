#include "NodeGraphTopology.hpp"

#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include <unordered_set>
#include <vector>

NodeGraphTopology::NodeGraphTopology(const NodeGraphState& state)
{
    nodeById.reserve(state.nodes.size());
    for (const NodeGraphNode& node : state.nodes) {
        nodeById.emplace(node.id.value, &node);
    }

    incomingEdgeByInputSocket.reserve(state.edges.size());
    for (const NodeGraphEdge& edge : state.edges) {
        incomingEdgeByInputSocket.emplace(makeSocketKey(edge.toNode, edge.toSocket), &edge);
    }
}

const NodeGraphNode* NodeGraphTopology::findNode(NodeGraphNodeId nodeId) const {
    const auto it = nodeById.find(nodeId.value);
    return it != nodeById.end() ? it->second : nullptr;
}

const NodeGraphEdge* NodeGraphTopology::findIncomingEdge(NodeGraphNodeId toNodeId, NodeGraphSocketId toSocketId) const {
    const auto it = incomingEdgeByInputSocket.find(makeSocketKey(toNodeId, toSocketId));
    return it != incomingEdgeByInputSocket.end() ? it->second : nullptr;
}

bool NodeGraphTopology::findFirstUpstreamNodeByType(
    NodeGraphNodeId startNodeId,
    const NodeTypeId& targetTypeId,
    NodeGraphNodeId& outNodeId) const {
    outNodeId = {};
    if (!startNodeId.isValid()) {
        return false;
    }

    std::vector<NodeGraphNodeId> stack{startNodeId};
    std::unordered_set<uint32_t> visitedNodeIds;
    visitedNodeIds.insert(startNodeId.value);

    while (!stack.empty()) {
        const NodeGraphNodeId currentNodeId = stack.back();
        stack.pop_back();

        const NodeGraphNode* currentNode = findNode(currentNodeId);
        if (!currentNode) {
            continue;
        }

        for (const NodeGraphSocket& inputSocket : currentNode->inputs) {
            if (inputSocket.valueType != NodeGraphValueType::Mesh) {
                continue;
            }

            const NodeGraphEdge* incomingEdge = findIncomingEdge(currentNodeId, inputSocket.id);
            if (!incomingEdge || !incomingEdge->fromNode.isValid()) {
                continue;
            }
            if (!visitedNodeIds.insert(incomingEdge->fromNode.value).second) {
                continue;
            }

            const NodeGraphNode* upstreamNode = findNode(incomingEdge->fromNode);
            if (!upstreamNode) {
                continue;
            }
            if (getNodeTypeId(upstreamNode->typeId) == targetTypeId) {
                outNodeId = upstreamNode->id;
                return true;
            }

            stack.push_back(upstreamNode->id);
        }
    }

    return false;
}

bool NodeGraphTopology::findFirstUpstreamNodeByType(
    uint64_t outputSocketKey,
    const NodeTypeId& targetTypeId,
    NodeGraphNodeId& outNodeId) const {
    outNodeId = {};

    NodeGraphNodeId producerNodeId{};
    NodeGraphSocketId producerSocketId{};
    if (!tryDecodeSocketKey(outputSocketKey, producerNodeId, producerSocketId)) {
        return false;
    }

    const NodeGraphNode* producerNode = findNode(producerNodeId);
    if (!producerNode) {
        return false;
    }
    if (getNodeTypeId(producerNode->typeId) == targetTypeId) {
        outNodeId = producerNode->id;
        return true;
    }

    return findFirstUpstreamNodeByType(producerNodeId, targetTypeId, outNodeId);
}
