#pragma once

#include <atomic>
#include <memory>

#include "framegraph/FrameSync.hpp"
#include "contact/ContactSystemController.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "runtime/ComputeCache.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "runtime/RuntimePayloadController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "heat/VoronoiSystem.hpp"
#include "heat/VoronoiSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "app/SwapchainManager.hpp"
#include "render/RenderRuntime.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

class RuntimeSimulationController;
class SceneContext;
class VulkanCoreContext;
struct WindowRuntimeState;

class RenderContext {
public:
    ~RenderContext();

    bool initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState,std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown);
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
    VoronoiSystem* voronoiSystem();
    HeatSystemController* heatSystemController();
    ContactSystemController* contactSystemController();
    ContactPreviewStore* contactPreviewStore();
    RuntimePayloadController* runtimePayloadController();
    SceneController* sceneController();
    const SceneController* sceneController() const;
    NodeGraphBridge* nodeGraphBridge();
    const NodeGraphBridge* nodeGraphBridge() const;
    NodeGraphController* nodeGraphController();
    InputController* inputController();

private:
    SwapchainManager swapchainManager;
    std::unique_ptr<RenderRuntime> renderRuntime;
    FrameSync frameSync;
    std::unique_ptr<VoronoiSystemController> voronoiSystemControllerState;
    std::unique_ptr<HeatSystemController> heatSystemControllerState;
    std::unique_ptr<ContactSystemController> contactSystemControllerState;
    std::unique_ptr<ContactPreviewStore> contactPreviewStoreState;
    std::unique_ptr<ComputeCache> computeCacheState;
    std::unique_ptr<RuntimeIntrinsicCache> intrinsicRegistryState;
    std::unique_ptr<RuntimePayloadController> runtimePayloadControllerState;
    std::unique_ptr<SceneController> sceneControllerState;
    std::unique_ptr<InputController> inputControllerState;
    std::unique_ptr<NodeGraphBridge> nodeGraphBridgeState;
    std::unique_ptr<NodeGraphController> nodeGraphControllerState;
    std::unique_ptr<NodePayloadRegistry> payloadRegistryState;
    bool initialized = false;
    bool inputPipelineInitialized = false;
};
