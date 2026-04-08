#pragma once

#include "NodeGraphTypes.hpp"

#include <string>

class NodeGraphEditor;
struct NodeGraphNode;

struct ModelPreviewSettings {
    bool showWireframe = false;
};

struct ModelNodeParams {
    std::string path;
    ModelPreviewSettings preview{};
};

ModelNodeParams readModelNodeParams(const NodeGraphNode& node);
bool writeModelNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ModelNodeParams& params);
