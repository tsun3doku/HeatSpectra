#pragma once

#include "NodeGraphTypes.hpp"
#include "heat/HeatGpuStructs.hpp"

class NodeGraphEditor;
struct NodeGraphNode;

struct HeatPreviewSettings {
    bool showHeatOverlay = false;
    bool showFluxVectors = false;
    bool showHeatPalette = false;
    bool showContactLevelSet = false;
    double fluxVectorScale = 1.0;
    double contactLevelSetRange = 1.0;
};

struct HeatSolveNodeParams {
    bool enabled = false;
    bool paused = false;
    uint32_t resetCounter = 0;
    uint32_t rewindFrame = heat::NoRewindFrame;
    double contactThermalConductance = 16000.0;
    double simulationDuration = 5.0;
    HeatPreviewSettings preview{};
};

HeatSolveNodeParams readHeatSolveNodeParams(const NodeGraphNode& node);
bool writeHeatSolveNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const HeatSolveNodeParams& params);
