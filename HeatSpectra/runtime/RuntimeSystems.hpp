#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "app/AppTypes.hpp"

#include "RenderSettingsController.hpp"
#include "RenderSettingsManager.hpp"
#include "RenderContext.hpp"
#include "RuntimeController.hpp"
#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"

class NodeGraphBridge;
class RuntimeQuery;
struct WindowRuntimeState;

class RuntimeSystems {
public:
    RuntimeSystems() = default;
    ~RuntimeSystems();

    bool initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext);
    void tickFrame(float deltaTime);
    void shutdown();
    bool isInitialized() const;
    AppViewportOutput getViewportOutput() const;

    const RuntimeQuery* runtimeQuery() const;
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    void setPanSensitivity(float sensitivity);
    void setRenderPaused(bool paused);
    RenderSettingsController* getSettingsController();
    const RenderSettingsController* getSettingsController() const;
    NodeGraphBridge* getNodeGraphBridge();
    const NodeGraphBridge* getNodeGraphBridge() const;

private:
    void updateViewportOutput(uint32_t frameIndex);
    void cleanup();

    WindowRuntimeState* windowRuntimeState = nullptr;
    bool initialized = false;
    std::atomic<bool> renderPaused{false};
    std::atomic<bool> runtimeBusy{false};
    std::atomic<bool> isShuttingDown{false};
    uint32_t frameCounter = 0;
    AppViewportOutput viewportOutput{};

    VulkanCoreContext core;
    SceneContext scene;
    RenderContext render;
    RuntimeController runtimeController;

    RenderSettingsManager settingsManager;
    RenderSettingsController settingsController{&settingsManager};
};

