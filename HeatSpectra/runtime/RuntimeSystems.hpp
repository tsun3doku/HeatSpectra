#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "app/AppTypes.hpp"
#include "RenderSettingsController.hpp"
#include "RenderSettingsManager.hpp"
#include "RenderContext.hpp"
#include "RuntimeController.hpp"
#include "SimulationError.hpp"
#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"

class NodeGraphBridge;
class ModelSelection;
class RuntimeQuery;
class SceneController;
struct WindowRuntimeState;

class RuntimeSystems {
public:
    RuntimeSystems() = default;
    ~RuntimeSystems();

    bool initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext);
    void tickFrame(float deltaTime);
    void shutdown();
    bool isInitialized() const;

    const RuntimeQuery* runtimeQuery() const;
    std::vector<SimulationError> consumeSimulationErrors();
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    void setPanSensitivity(float sensitivity);
    void setRenderPaused(bool paused);
    RenderSettingsController* getSettingsController();
    const RenderSettingsController* getSettingsController() const;
    NodeGraphBridge* getNodeGraphBridge();
    const NodeGraphBridge* getNodeGraphBridge() const;
    SceneController* getSceneController();
    const SceneController* getSceneController() const;
    ModelSelection* getModelSelection();
    const ModelSelection* getModelSelection() const;

private:
    void cleanup();

    WindowRuntimeState* windowRuntimeState = nullptr;
    bool initialized = false;
    std::atomic<bool> renderPaused{false};
    std::atomic<bool> runtimeBusy{false};
    std::atomic<bool> isShuttingDown{false};
    uint32_t frameCounter = 0;

    VulkanCoreContext core;
    SceneContext scene;
    RenderContext render;
    RuntimeController runtimeController;

    RenderSettingsManager settingsManager;
    RenderSettingsController settingsController{&settingsManager};
};
