#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "RuntimeExecutionController.hpp"
#include "RuntimeInputController.hpp"
#include "RuntimeRenderController.hpp"
#include "RuntimeSimulationController.hpp"

class RenderContext;
class RenderSettingsManager;
class RuntimeQuery;
class SceneContext;
class VulkanCoreContext;
struct WindowRuntimeState;

class RuntimeController {
public:
    ~RuntimeController();

    bool initialize(
        RenderContext& render,
        SceneContext& scene,
        VulkanCoreContext& core,
        WindowRuntimeState& windowRuntimeState,
        RenderSettingsManager& settingsManager,
        std::atomic<bool>& renderPaused);
    void shutdown();
    bool isInitialized() const;
    void tick(float deltaTime, uint32_t& frameCounter);

    const RuntimeQuery* runtimeQuery() const;
    RuntimeSimulationController* simulationController();
    const RuntimeSimulationController* simulationController() const;
    bool hasLastFrameSlot() const;
    uint32_t lastFrameSlot() const;

private:
    std::unique_ptr<RuntimeSimulationController> runtimeSimulationController;
    std::unique_ptr<RuntimeInputController> runtimeInputController;
    std::unique_ptr<RuntimeRenderController> runtimeRenderController;
    std::unique_ptr<RuntimeExecutionController> runtimeExecutionController;
    bool initialized = false;
};
