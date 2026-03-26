#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/AppTypes.hpp"
#include "RuntimeSystems.hpp"
#include "SimulationError.hpp"

class NodeGraphBridge;
class ModelSelection;
class RenderSettingsController;
class RuntimeQuery;
class SceneController;
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
    std::unique_ptr<RuntimeSystems> systems;
};
