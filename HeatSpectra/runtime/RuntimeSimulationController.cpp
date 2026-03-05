#include "RuntimeSimulationController.hpp"

#include "RenderSettingsManager.hpp"
#include "heat/HeatSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphTypes.hpp"
#include "scene/SceneController.hpp"

#include <utility>

RuntimeSimulationController::RuntimeSimulationController(
    HeatSystemController& heatSystemController,
    SceneController& sceneController,
    NodeGraphBridge& nodeGraphBridge,
    NodeGraphController& nodeGraphController,
    RenderSettingsManager& settingsManager)
    : heatSystemController(heatSystemController),
      sceneController(sceneController),
      nodeGraphBridge(nodeGraphBridge),
      nodeGraphController(nodeGraphController),
      settingsManager(settingsManager) {
}

bool RuntimeSimulationController::canExecuteHeatSolve() const {
    return nodeGraphController.canExecuteHeatSolve();
}

bool RuntimeSimulationController::canStartHeatSolve(std::string& reason, SimulationErrorCode& errorCode) const {
    const NodeGraphState graphState = nodeGraphBridge.state();
    bool hasEnabledHeatNode = false;
    for (const NodeGraphNode& node : graphState.nodes) {
        bool enabled = false;
        if (canonicalNodeTypeId(node.typeId) == nodegraphtypes::HeatSolve &&
            tryGetNodeParamBool(node, nodegraphparams::heatsolve::Enabled, enabled) &&
            enabled) {
            hasEnabledHeatNode = true;
            break;
        }
    }

    if (!hasEnabledHeatNode) {
        reason = "No enabled Heat Solver node exists";
        errorCode = SimulationErrorCode::NoEnabledHeatSolveNode;
        return false;
    }

    std::string blockReason;
    if (!nodeGraphBridge.canExecuteHeatSolve(blockReason)) {
        reason = blockReason.empty() ? std::string("Heat Solver graph is blocked") : std::move(blockReason);
        errorCode = SimulationErrorCode::HeatSolveGraphBlocked;
        return false;
    }

    reason.clear();
    errorCode = SimulationErrorCode::Unknown;
    return true;
}

uint32_t RuntimeSimulationController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    return sceneController.loadModel(modelPath, preferredModelId);
}

std::vector<SimulationError> RuntimeSimulationController::consumeSimulationErrors() {
    std::lock_guard<std::mutex> lock(simulationErrorMutex);
    std::vector<SimulationError> errors;
    errors.swap(pendingSimulationErrors);
    return errors;
}

bool RuntimeSimulationController::isSimulationActive() const {
    return heatSystemController.isHeatSystemActive();
}

bool RuntimeSimulationController::isSimulationPaused() const {
    return heatSystemController.isHeatSystemPaused();
}

void RuntimeSimulationController::toggleSimulation() {
    if (!heatSystemController.isHeatSystemActive()) {
        std::string blockReason;
        SimulationErrorCode errorCode = SimulationErrorCode::Unknown;
        if (!canStartHeatSolve(blockReason, errorCode)) {
            std::string errorMessage = "Cannot start Heat Solve";
            if (!blockReason.empty()) {
                errorMessage += ": " + blockReason;
            }
            pushSimulationError(errorCode, std::move(errorMessage));
            return;
        }
    }

    heatSystemController.toggleHeatSystem();
}

void RuntimeSimulationController::pushSimulationError(SimulationErrorCode code, std::string message) {
    std::lock_guard<std::mutex> lock(simulationErrorMutex);
    pendingSimulationErrors.push_back({code, std::move(message)});
}

void RuntimeSimulationController::pauseSimulation() {
    heatSystemController.pauseHeatSystem();
}

void RuntimeSimulationController::resetSimulation() {
    heatSystemController.resetHeatSystem();
}

void RuntimeSimulationController::onWireframeToggleRequested() {
    settingsManager.toggleWireframeMode();
}

void RuntimeSimulationController::onIntrinsicOverlayToggleRequested() {
    settingsManager.toggleIntrinsicOverlay();
}

void RuntimeSimulationController::onHeatOverlayToggleRequested() {
    settingsManager.toggleHeatOverlay();
}

void RuntimeSimulationController::onTimingOverlayToggleRequested() {
    settingsManager.toggleTimingOverlay();
}

void RuntimeSimulationController::onSimulationToggleRequested() {
    toggleSimulation();
}

void RuntimeSimulationController::onSimulationPauseRequested() {
    pauseSimulation();
}

void RuntimeSimulationController::onSimulationResetRequested() {
    resetSimulation();
}
