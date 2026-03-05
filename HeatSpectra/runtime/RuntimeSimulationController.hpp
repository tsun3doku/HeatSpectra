#pragma once

#include "runtime/RuntimeInterfaces.hpp"
#include "runtime/SimulationError.hpp"
#include "scene/InputActions.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class HeatSystemController;
class NodeGraphBridge;
class NodeGraphController;
class RenderSettingsManager;
class SceneController;

class RuntimeSimulationController : public InputActionHandler, public RuntimeCommands, public RuntimeQuery {
public:
    RuntimeSimulationController(
        HeatSystemController& heatSystemController,
        SceneController& sceneController,
        NodeGraphBridge& nodeGraphBridge,
        NodeGraphController& nodeGraphController,
        RenderSettingsManager& settingsManager);

    bool canExecuteHeatSolve() const;
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    std::vector<SimulationError> consumeSimulationErrors();

    bool isSimulationActive() const override;
    bool isSimulationPaused() const override;
    void toggleSimulation() override;
    void pauseSimulation() override;
    void resetSimulation() override;

private:
    bool canStartHeatSolve(std::string& reason, SimulationErrorCode& errorCode) const;
    void pushSimulationError(SimulationErrorCode code, std::string message);
    void onWireframeToggleRequested() override;
    void onIntrinsicOverlayToggleRequested() override;
    void onHeatOverlayToggleRequested() override;
    void onTimingOverlayToggleRequested() override;
    void onSimulationToggleRequested() override;
    void onSimulationPauseRequested() override;
    void onSimulationResetRequested() override;

    HeatSystemController& heatSystemController;
    SceneController& sceneController;
    NodeGraphBridge& nodeGraphBridge;
    NodeGraphController& nodeGraphController;
    RenderSettingsManager& settingsManager;
    std::mutex simulationErrorMutex;
    std::vector<SimulationError> pendingSimulationErrors;
};
