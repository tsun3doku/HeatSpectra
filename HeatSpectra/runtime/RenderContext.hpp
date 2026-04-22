#pragma once

#include <atomic>
#include <memory>

#include "framegraph/FrameSync.hpp"
#include "contact/ContactSystemComputeController.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/ModelComputeRuntime.hpp"
#include "runtime/ModelDisplayRuntime.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/RuntimeContactComputeTransport.hpp"
#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/HeatDisplayController.hpp"
#include "runtime/RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimeModelComputeTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeVoronoiComputeTransport.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"
#include "runtime/VoronoiDisplayController.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "heat/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "app/SwapchainManager.hpp"
#include "render/RenderRuntime.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

class RenderSettingsController;
class SceneContext;
class VulkanCoreContext;
struct WindowRuntimeState;

class RenderContext {
public:
    ~RenderContext();

    bool initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState, RenderSettingsController* renderSettingsController, std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown);
    bool initializeInputPipeline(SceneContext& scene, InputActionHandler& inputActions);
    bool initializeSyncObjects();
    void shutdown();
    bool isInitialized() const;

    SwapchainManager& swapchain();
    const SwapchainManager& swapchain() const;
    FrameSync& sync();
    RenderRuntime* runtime();
    const RenderRuntime* runtime() const;
    HeatSystemComputeController* heatSystemComputeController();
    const HeatSystemComputeController* heatSystemComputeController() const;
    ContactSystemComputeController* contactSystemComputeController();
    ModelComputeRuntime* modelComputeRuntime();
    const ModelComputeRuntime* modelComputeRuntime() const;
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
    std::unique_ptr<RuntimeContactComputeTransport> runtimeContactComputeTransportState;
    std::unique_ptr<RuntimeContactDisplayTransport> runtimeContactDisplayTransportState;
    std::unique_ptr<RuntimeHeatComputeTransport> runtimeHeatComputeTransportState;
    std::unique_ptr<RuntimeHeatDisplayTransport> runtimeHeatDisplayTransportState;
    std::unique_ptr<RuntimeModelComputeTransport> runtimeModelComputeTransportState;
    std::unique_ptr<RuntimeModelDisplayTransport> runtimeModelDisplayTransportState;
    std::unique_ptr<RuntimeRemeshDisplayTransport> runtimeRemeshDisplayTransportState;
    std::unique_ptr<RuntimeRemeshComputeTransport> runtimeRemeshTransportState;
    std::unique_ptr<RuntimeVoronoiComputeTransport> runtimeVoronoiComputeTransportState;
    std::unique_ptr<RuntimeVoronoiDisplayTransport> runtimeVoronoiDisplayTransportState;
    std::unique_ptr<ModelComputeRuntime> modelComputeRuntimeState;
    std::unique_ptr<ModelDisplayRuntime> modelDisplayRuntimeState;
    std::unique_ptr<RemeshController> remeshControllerState;
    std::unique_ptr<RemeshDisplayController> remeshDisplayControllerState;
    std::unique_ptr<ContactDisplayController> contactDisplayControllerState;
    std::unique_ptr<HeatDisplayController> heatDisplayControllerState;
    std::unique_ptr<VoronoiDisplayController> voronoiDisplayControllerState;
    std::unique_ptr<VoronoiSystemComputeController> voronoiSystemComputeControllerState;
    std::unique_ptr<HeatSystemComputeController> heatSystemComputeControllerState;
    std::unique_ptr<ContactSystemComputeController> contactSystemComputeControllerState;
    std::unique_ptr<SceneController> sceneControllerState;
    std::unique_ptr<InputController> inputControllerState;
    std::unique_ptr<NodeGraphBridge> nodeGraphBridgeState;
    std::unique_ptr<NodeGraphRuntimeBridge> nodeGraphRuntimeBridgeState;
    std::unique_ptr<NodeGraphController> nodeGraphControllerState;
    std::unique_ptr<NodePayloadRegistry> payloadRegistryState;
    bool initialized = false;
    bool inputPipelineInitialized = false;
};
