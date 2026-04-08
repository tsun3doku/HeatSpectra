#pragma once

#include "runtime/RuntimeInterfaces.hpp"
#include "runtime/SimulationError.hpp"
#include "scene/InputActions.hpp"

#include <cstdint>
#include <vector>

class HeatSystemController;
class ModelRuntime;
class NodeGraphBridge;
class NodeGraphController;
class RenderSettingsManager;

class RuntimeSimulationController : public InputActionHandler, public RuntimeQuery {
public:
    RuntimeSimulationController(
        HeatSystemController& heatSystemController,
        ModelRuntime& modelRuntime,
        NodeGraphController& nodeGraphController,
        RenderSettingsManager& settingsManager);

    bool canExecuteHeatSolve() const;
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    std::vector<SimulationError> consumeSimulationErrors();

    bool isSimulationActive() const override;
    bool isSimulationPaused() const override;

private:
    void onWireframeToggleRequested() override;
    void onIntrinsicOverlayToggleRequested() override;
    void onHeatOverlayToggleRequested() override;
    void onTimingOverlayToggleRequested() override;

    HeatSystemController& heatSystemController;
    ModelRuntime& modelRuntime;
    NodeGraphController& nodeGraphController;
    RenderSettingsManager& settingsManager;
};
