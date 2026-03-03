#include "NodeSolverController.hpp"

#include "heat/HeatSystemController.hpp"
#include "scene/ModelRegistry.hpp"

#include <unordered_set>

NodeSolverController::NodeSolverController(ModelRegistry& modelRegistryRef, HeatSystemController& heatSystemControllerRef)
    : modelRegistry(modelRegistryRef),
      heatSystemController(heatSystemControllerRef) {
}

void NodeSolverController::setHeatSolveModelRoles(
    const std::vector<uint32_t>& sourceNodeModelIds,
    const std::vector<uint32_t>& receiverNodeModelIds) {
    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
    std::unordered_set<uint32_t> seenReceiverRuntimeModelIds;

    for (uint32_t nodeModelId : sourceNodeModelIds) {
        uint32_t runtimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(nodeModelId, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        if (seenSourceRuntimeModelIds.insert(runtimeModelId).second) {
            sourceRuntimeModelIds.push_back(runtimeModelId);
        }
    }

    for (uint32_t nodeModelId : receiverNodeModelIds) {
        uint32_t runtimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(nodeModelId, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        if (seenReceiverRuntimeModelIds.insert(runtimeModelId).second) {
            receiverRuntimeModelIds.push_back(runtimeModelId);
        }
    }

    heatSystemController.setActiveModels(sourceRuntimeModelIds, receiverRuntimeModelIds);
}

bool NodeSolverController::isHeatSolveActive() const {
    return heatSystemController.isHeatSystemActive();
}

bool NodeSolverController::isHeatSolvePaused() const {
    return heatSystemController.isHeatSystemPaused();
}

bool NodeSolverController::deactivateHeatSolveIfActive() {
    if (!heatSystemController.isHeatSystemActive()) {
        return false;
    }

    heatSystemController.toggleHeatSystem();
    return true;
}

bool NodeSolverController::ensureHeatSolveRunningState(bool wantsPaused) {
    bool isActive = heatSystemController.isHeatSystemActive();
    bool isPaused = heatSystemController.isHeatSystemPaused();
    bool changed = false;

    if (!isActive) {
        heatSystemController.toggleHeatSystem();
        isActive = heatSystemController.isHeatSystemActive();
        isPaused = heatSystemController.isHeatSystemPaused();
        changed = true;
    }

    if (!isActive) {
        return changed;
    }

    if (wantsPaused && !isPaused) {
        heatSystemController.pauseHeatSystem();
        return true;
    }

    if (!wantsPaused && isPaused) {
        heatSystemController.toggleHeatSystem();
        return true;
    }

    return changed;
}

void NodeSolverController::resetHeatSolve() {
    heatSystemController.resetHeatSystem();
}
