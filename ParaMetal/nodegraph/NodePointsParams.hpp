#pragma once

#include "NodeGraphTypes.hpp"
#include <cstdint>

class NodeGraphEditor;
struct NodeGraphNode;

struct PointsNodeParams {
    uint32_t pointCount = 1000;
    float dimX = 1.0f;
    float dimY = 1.0f;
    float dimZ = 1.0f;
};

PointsNodeParams readPointsNodeParams(const NodeGraphNode& node);
bool writePointsNodeParams(
    NodeGraphEditor& editor,
    NodeGraphNodeId nodeId,
    const PointsNodeParams& params);
