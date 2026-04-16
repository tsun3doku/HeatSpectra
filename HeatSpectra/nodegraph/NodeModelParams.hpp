#pragma once

#include "NodeGraphTypes.hpp"

#include <string>

class NodeGraphEditor;
struct NodeGraphNode;

struct ModelNodeParams {
    std::string path;
};

ModelNodeParams readModelNodeParams(const NodeGraphNode& node);
bool writeModelNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ModelNodeParams& params);
