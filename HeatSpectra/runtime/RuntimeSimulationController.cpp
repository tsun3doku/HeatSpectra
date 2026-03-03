#include "RuntimeSimulationController.hpp"

#include "RenderSettingsManager.hpp"
#include "heat/HeatSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphTypes.hpp"
#include "scene/SceneController.hpp"

#include <iostream>
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

bool RuntimeSimulationController::canStartHeatSolve(std::string& reason) const {
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
        return false;
    }

    std::string blockReason;
    if (!nodeGraphBridge.canExecuteHeatSolve(blockReason)) {
        reason = blockReason.empty() ? std::string("Heat Solver graph is blocked") : std::move(blockReason);
        return false;
    }

    reason.clear();
    return true;
}

uint32_t RuntimeSimulationController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    return sceneController.loadModel(modelPath, preferredModelId);
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
        if (!canStartHeatSolve(blockReason)) {
            std::cerr << "[NodeGraph] Cannot start Heat Solve";
            if (!blockReason.empty()) {
                std::cerr << ": " << blockReason;
            }
            std::cerr << std::endl;
            return;
        }
    }

    heatSystemController.toggleHeatSystem();
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
