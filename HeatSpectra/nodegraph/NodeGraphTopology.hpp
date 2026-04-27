#pragma once

#include "NodeGraphTypes.hpp"

#include <unordered_map>

class NodeGraphTopology {
public:
    explicit NodeGraphTopology(const NodeGraphState& state);

    const NodeGraphNode* findNode(NodeGraphNodeId nodeId) const;
    const NodeGraphEdge* findIncomingEdge(NodeGraphNodeId toNodeId, NodeGraphSocketId toSocketId) const;

    bool findFirstUpstreamNodeByType(NodeGraphNodeId startNodeId, const NodeTypeId& targetTypeId, NodeGraphNodeId& outNodeId) const;
    bool findFirstUpstreamNodeByType(uint64_t outputSocketKey, const NodeTypeId& targetTypeId, NodeGraphNodeId& outNodeId) const;

private:
    std::unordered_map<uint32_t, const NodeGraphNode*> nodeById;
    std::unordered_map<uint64_t, const NodeGraphEdge*> incomingEdgeByInputSocket;
};