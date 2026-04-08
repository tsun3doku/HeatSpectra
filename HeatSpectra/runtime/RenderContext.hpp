#pragma once

#include <atomic>
#include <memory>

#include "framegraph/FrameSync.hpp"
#include "contact/ContactSystemController.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/ModelRuntime.hpp"
#include "runtime/RuntimeContactTransport.hpp"
#include "runtime/RuntimeHeatTransport.hpp"
#include "runtime/RuntimeModelTransport.hpp"
#include "runtime/RuntimePackageController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimeRemeshTransport.hpp"
#include "runtime/RuntimeVoronoiTransport.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "heat/VoronoiSystem.hpp"
#include "heat/VoronoiSystemController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "app/SwapchainManager.hpp"
#include "render/RenderRuntime.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

class RuntimeSimulationController;
class RenderSettingsController;
class SceneContext;
class VulkanCoreContext;
struct WindowRuntimeState;

class RenderContext {
public:
    ~RenderContext();

    bool initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState, RenderSettingsController* renderSettingsController, std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown);
    bool initializeInputPipeline(SceneContext& scene, RuntimeSimulationController& simulationController);
    bool initializeSyncObjects();
    void shutdown();
    bool isInitialized() const;

    SwapchainManager& swapchain();
    const SwapchainManager& swapchain() const;
    FrameSync& sync();
    RenderRuntime* runtime();
    const RenderRuntime* runtime() const;
    HeatSystemController* heatSystemController();
    ContactSystemController* contactSystemController();
    ModelRuntime* modelRuntime();
    const ModelRuntime* modelRuntime() const;
    RuntimePackageController* runtimePackageController();
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
    std::unique_ptr<RuntimeContactTransport> runtimeContactTransportState;
    std::unique_ptr<RuntimeHeatTransport> runtimeHeatTransportState;
    std::unique_ptr<RuntimeModelTransport> runtimeModelTransportState;
    std::unique_ptr<RuntimeRemeshTransport> runtimeRemeshTransportState;
    std::unique_ptr<RuntimeVoronoiTransport> runtimeVoronoiTransportState;
    std::unique_ptr<RuntimeProductRegistry> runtimeProductRegistryState;
    std::unique_ptr<ModelRuntime> modelRuntimeState;
    std::unique_ptr<RemeshController> remeshControllerState;
    std::unique_ptr<VoronoiSystemController> voronoiSystemControllerState;
    std::unique_ptr<HeatSystemController> heatSystemControllerState;
    std::unique_ptr<ContactSystemController> contactSystemControllerState;
    std::unique_ptr<RuntimePackageController> runtimePackageControllerState;
    std::unique_ptr<SceneController> sceneControllerState;
    std::unique_ptr<InputController> inputControllerState;
    std::unique_ptr<NodeGraphBridge> nodeGraphBridgeState;
    std::unique_ptr<NodeGraphRuntimeBridge> nodeGraphRuntimeBridgeState;
    std::unique_ptr<NodeGraphController> nodeGraphControllerState;
    std::unique_ptr<NodePayloadRegistry> payloadRegistryState;
    bool initialized = false;
    bool inputPipelineInitialized = false;
};
