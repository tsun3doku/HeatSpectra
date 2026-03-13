#pragma once

#include <atomic>
#include <memory>

#include "framegraph/FrameSync.hpp"
#include "heat/ContactSystemController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeSolverController.hpp"
#include "app/SwapchainManager.hpp"
#include "render/RenderRuntime.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

class ModelRegistry;
class RuntimeSimulationController;
class SceneContext;
class VulkanCoreContext;
struct WindowRuntimeState;

class RenderContext {
public:
    ~RenderContext();

    bool initialize(
        VulkanCoreContext& core,
        SceneContext& scene,
        WindowRuntimeState& windowState,
        std::atomic<bool>& runtimeBusy,
        std::atomic<bool>& isShuttingDown);
    bool initializeInputPipeline(SceneContext& scene, RuntimeSimulationController& simulationController);
    bool initializeSyncObjects();
    void shutdown();
    bool isInitialized() const;

    SwapchainManager& swapchain();
    const SwapchainManager& swapchain() const;
    FrameSync& sync();
    RenderRuntime* runtime();
    const RenderRuntime* runtime() const;
    HeatSystem* heatSystem();
    HeatSystemController* heatSystemController();
    ContactSystemController* contactSystemController();
    SceneController* sceneController();
    NodeGraphBridge* nodeGraphBridge();
    const NodeGraphBridge* nodeGraphBridge() const;
    NodeGraphController* nodeGraphController();
    InputController* inputController();

private:
    SwapchainManager swapchainManager;
    std::unique_ptr<RenderRuntime> renderRuntime;
    FrameSync frameSync;
    std::unique_ptr<HeatSystem> heatSystemState;
    std::unique_ptr<HeatSystemController> heatSystemControllerState;
    std::unique_ptr<ContactSystemController> contactSystemControllerState;
    std::unique_ptr<SceneController> sceneControllerState;
    std::unique_ptr<NodeSolverController> nodeSolverController;
    std::unique_ptr<InputController> inputControllerState;
    std::unique_ptr<NodeGraphBridge> nodeGraphBridgeState;
    std::unique_ptr<NodeGraphController> nodeGraphControllerState;
    ModelRegistry* modelRegistry = nullptr;
    bool initialized = false;
    bool inputPipelineInitialized = false;
};
