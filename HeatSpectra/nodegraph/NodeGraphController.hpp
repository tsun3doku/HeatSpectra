#pragma once

#include "NodeGraphCompiler.hpp"
#include "NodeGraphRuntime.hpp"
#include "runtime/RuntimePackageSync.hpp"

#include <cstdint>
#include <unordered_map>

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
    struct OutputPreviewState {
        bool enabled = false;
    };

    static bool allChangesAreLayout(const NodeGraphDelta& delta);
    void updateNodeOwnedRenderSettings();
    void updateContactPreviews(const NodeGraphEvaluationState& execState);

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphRuntime runtime;
    uint64_t revisionSeen = 0;
    NodeGraphCompiled plan{};
    RuntimePackageSet runtimePackages{};
    RuntimePackageSync runtimePackageSync{};
    std::unordered_map<uint64_t, OutputPreviewState> previewStateBySocket;
};
