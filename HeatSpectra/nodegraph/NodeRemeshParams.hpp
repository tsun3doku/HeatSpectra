#pragma once

#include "NodeGraphTypes.hpp"

class NodeGraphEditor;
struct NodeGraphNode;

struct RemeshPreviewSettings {
    bool showRemeshOverlay = false;
    bool showFaceNormals = false;
    bool showVertexNormals = false;
};

struct RemeshNodeParams {
    int iterations = 10;
    double minAngleDegrees = 25.0;
    double maxEdgeLength = 0.2;
    double stepSize = 0.5;
    RemeshPreviewSettings preview{};
    double normalLength = 0.05;
};

RemeshNodeParams readRemeshNodeParams(const NodeGraphNode& node);
bool writeRemeshNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const RemeshNodeParams& params);
