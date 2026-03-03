#pragma once

#include "NodeGraphExecutionPlanner.hpp"
#include "NodeGraphRuntime.hpp"

#include <cstdint>

class NodeGraphBridge;

class NodeGraphController {
public:
    NodeGraphController(
        NodeGraphBridge* bridge = nullptr,
        const NodeRuntimeServices& services = {});

    void applyPendingChanges();
    void tick();
    bool canExecuteHeatSolve() const;
    const NodeGraphExecutionPlan& executionPlan() const;

private:
    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphRuntime runtime;
    uint64_t revisionSeen = 0;
    NodeGraphExecutionPlan plan{};
};
