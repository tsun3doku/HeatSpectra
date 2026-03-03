#pragma once

#include "NodeGraphDocument.hpp"

#include <string>
#include <vector>

class NodeGraphValidator {
public:
    static bool canCreateConnection(
        const NodeGraphDocument& document,
        NodeGraphNodeId fromNode,
        NodeGraphSocketId fromSocket,
        NodeGraphNodeId toNode,
        NodeGraphSocketId toSocket,
        std::string& errorMessage,
        NodeGraphEdgeId ignoreExistingEdge = {});

private:
    static bool wouldIntroduceCycle(const NodeGraphDocument& document, NodeGraphNodeId fromNode, NodeGraphNodeId toNode);
};
