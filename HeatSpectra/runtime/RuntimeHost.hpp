#pragma once

#include <memory>
#include <string>

#include "app/AppTypes.hpp"
#include "RuntimeSystems.hpp"

class NodeGraphBridge;
class RenderSettingsController;
class RuntimeQuery;
struct WindowRuntimeState;

class RuntimeHost {
public:
    RuntimeHost();
    ~RuntimeHost();

    bool initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext);
    void tickFrame(float deltaTime);
    void shutdown();
    bool isInitialized() const;

    const RuntimeQuery* runtimeQuery() const;
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);

    void setPanSensitivity(float sensitivity);
    void setRenderPaused(bool paused);

    AppViewportOutput getViewportOutput() const;
    RenderSettingsController* getSettingsController();
    const RenderSettingsController* getSettingsController() const;
    NodeGraphBridge* getNodeGraphBridge();
    const NodeGraphBridge* getNodeGraphBridge() const;

private:
    std::unique_ptr<RuntimeSystems> systems;
};
