#pragma once

#include "NodeGraphTypes.hpp"

class NodeGraphEditor;
struct NodeGraphNode;

struct HeatSourceNodeParams {
    double temperature = 100.0;
};

HeatSourceNodeParams readHeatSourceNodeParams(const NodeGraphNode& node);
bool writeHeatSourceNodeParams(
    NodeGraphEditor& editor,
    NodeGraphNodeId nodeId,
    const HeatSourceNodeParams& params);
