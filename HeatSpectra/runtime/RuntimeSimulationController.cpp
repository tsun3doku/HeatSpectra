#include "RuntimeSimulationController.hpp"

#include "RenderSettingsManager.hpp"
#include "heat/HeatSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphTypes.hpp"
#include "runtime/ModelRuntime.hpp"

RuntimeSimulationController::RuntimeSimulationController(
    HeatSystemController& heatSystemController,
    ModelRuntime& modelRuntime,
    NodeGraphController& nodeGraphController,
    RenderSettingsManager& settingsManager)
    : heatSystemController(heatSystemController),
      modelRuntime(modelRuntime),
      nodeGraphController(nodeGraphController),
      settingsManager(settingsManager) {
}

bool RuntimeSimulationController::canExecuteHeatSolve() const {
    return nodeGraphController.canExecuteHeatSolve();
}

uint32_t RuntimeSimulationController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    return modelRuntime.loadModel(modelPath, preferredModelId);
}

std::vector<SimulationError> RuntimeSimulationController::consumeSimulationErrors() {
    return {};
}

bool RuntimeSimulationController::isSimulationActive() const {
    return heatSystemController.isAnyHeatSystemActive();
}

bool RuntimeSimulationController::isSimulationPaused() const {
    return heatSystemController.isAnyHeatSystemPaused();
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
