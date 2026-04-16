#pragma once

#include "NodeGraphDisplay.hpp"
#include "NodeGraphCompiler.hpp"
#include "NodeGraphRuntime.hpp"
#include "runtime/RuntimePackageSync.hpp"

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
    const NodeGraphCompiled& compiledState() const;

private:
    static bool allChangesAreLayout(const NodeGraphDelta& delta);

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphRuntime runtime;
    uint64_t revisionSeen = 0;
    NodeGraphCompiled plan{};
    RuntimePackageGraph fullRuntimePackageGraph{};
    RuntimePackageGraph displayRuntimePackageGraph{};
    NodeGraphDisplay nodeGraphDisplay{};
    RuntimePackageSync computePackageSync{};
    RuntimePackageSync displayPackageSync{};
};
