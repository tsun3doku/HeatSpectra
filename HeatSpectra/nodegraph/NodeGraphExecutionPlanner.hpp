#pragma once

#include "NodeGraphTypes.hpp"

#include <string>
#include <vector>

struct NodeGraphExecutionPlan {
    uint64_t revision = 0;
    bool hasHeatSolveNode = false;
    bool canExecuteHeatSolve = false;
    std::string heatSolveBlockReason;
};

class NodeGraphExecutionPlanner {
public:
    static bool nodeHasAllRequiredInputs(const NodeGraphState& state, NodeGraphNodeId nodeId);
    static bool buildTopologicalOrder(
        const NodeGraphState& state,
        std::vector<NodeGraphNodeId>& outOrder,
        std::string* outError = nullptr);
    static NodeGraphExecutionPlan buildPlan(const NodeGraphState& state);
};
