#pragma once

#include "NodeGraphTypes.hpp"

class NodeGraphEditor;
struct NodeGraphNode;

struct ContactPreviewSettings {
    bool showContactLines = false;
};

struct ContactNodeParams {
    double minNormalDot = -0.65;
    double contactRadius = 0.01;
    ContactPreviewSettings preview{};
};

ContactNodeParams readContactNodeParams(const NodeGraphNode& node);
bool writeContactNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ContactNodeParams& params);
