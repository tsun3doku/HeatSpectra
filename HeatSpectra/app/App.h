#pragma once

#include <memory>
#include <string>

#include "app/AppTypes.hpp"
#include "runtime/RuntimeHost.hpp"

class NodeGraphBridge;
class RenderSettingsController;
class RuntimeQuery;
struct WindowRuntimeState;

class App {
public:
    App();
    ~App();

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
    std::unique_ptr<RuntimeHost> runtimeHost;
};
