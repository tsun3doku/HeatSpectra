#pragma once

#include "NodeGraphTypes.hpp"

class NodeGraphEditor;
struct NodeGraphNode;

struct VoronoiPreviewSettings {
    bool showVoronoi = false;
    bool showPoints = false;
};

struct VoronoiNodeParams {
    double cellSize = 0.005;
    int voxelResolution = 128;
    VoronoiPreviewSettings preview{};
};

VoronoiNodeParams readVoronoiNodeParams(const NodeGraphNode& node);
bool writeVoronoiNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const VoronoiNodeParams& params);
