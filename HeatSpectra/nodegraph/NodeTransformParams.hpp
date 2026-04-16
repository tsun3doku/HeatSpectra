#pragma once

#include "NodeGraphTypes.hpp"

class NodeGraphBridge;
class NodeGraphEditor;
struct NodeGraphNode;

struct TransformNodeParams {
    double translateX = 0.0;
    double translateY = 0.0;
    double translateZ = 0.0;
    double rotateXDegrees = 0.0;
    double rotateYDegrees = 0.0;
    double rotateZDegrees = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;
};

TransformNodeParams readTransformNodeParams(const NodeGraphNode& node);
bool writeTransformNodeParams(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, const TransformNodeParams& params);
bool writeTransformNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const TransformNodeParams& params);
