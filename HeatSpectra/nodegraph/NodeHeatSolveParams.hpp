#pragma once

#include "NodeGraphTypes.hpp"
#include "NodeHeatMaterialPresets.hpp"

#include <vector>

class NodeGraphEditor;
struct NodeGraphNode;

struct HeatPreviewSettings {
    bool showHeatOverlay = false;
};

struct HeatSolveNodeParams {
    bool enabled = false;
    bool paused = false;
    bool resetRequested = false;
    double cellSize = 0.005;
    int voxelResolution = 128;
    HeatPreviewSettings preview{};
    std::vector<HeatMaterialBindingRow> materialBindingRows;
};

HeatSolveNodeParams readHeatSolveNodeParams(const NodeGraphNode& node);
bool writeHeatSolveNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const HeatSolveNodeParams& params);
std::vector<HeatMaterialBinding> makeHeatPayloadMaterialBindings(const HeatSolveNodeParams& params);
