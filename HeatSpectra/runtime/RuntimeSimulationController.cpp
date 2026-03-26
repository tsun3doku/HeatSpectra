#include "RuntimeSimulationController.hpp"

#include "RenderSettingsManager.hpp"
#include "heat/HeatSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphTypes.hpp"
#include "scene/SceneController.hpp"

RuntimeSimulationController::RuntimeSimulationController(
    HeatSystemController& heatSystemController,
    SceneController& sceneController,
    NodeGraphController& nodeGraphController,
    RenderSettingsManager& settingsManager)
    : heatSystemController(heatSystemController),
      sceneController(sceneController),
      nodeGraphController(nodeGraphController),
      settingsManager(settingsManager) {
}

bool RuntimeSimulationController::canExecuteHeatSolve() const {
    return nodeGraphController.canExecuteHeatSolve();
}

uint32_t RuntimeSimulationController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    return sceneController.loadModel(modelPath, preferredModelId);
}

std::vector<SimulationError> RuntimeSimulationController::consumeSimulationErrors() {
    return {};
}

bool RuntimeSimulationController::isSimulationActive() const {
    return heatSystemController.isHeatSystemActive();
}

bool RuntimeSimulationController::isSimulationPaused() const {
    return heatSystemController.isHeatSystemPaused();
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
