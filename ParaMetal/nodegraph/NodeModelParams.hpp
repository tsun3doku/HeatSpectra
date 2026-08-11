#pragma once

#include "NodeGraphTypes.hpp"
#include "util/Units.hpp"
#include <string>

class NodeGraphEditor;
struct NodeGraphNode;

struct ModelNodeParams {
    std::string path;
    units::LengthUnit sourceUnit = units::defaultLengthUnit();
};

ModelNodeParams readModelNodeParams(const NodeGraphNode& node);
bool writeModelNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const ModelNodeParams& params);
