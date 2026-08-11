#pragma once

#include <atomic>
#include <memory>

#include "app/AppTypes.hpp"
#include "framegraph/FrameSync.hpp"
#include "contact/ContactSystemComputeController.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/ModelComputeController.hpp"
#include "runtime/ModelDisplayController.hpp"
#include "runtime/PointComputeRuntime.hpp"
#include "runtime/PointDisplayController.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/RuntimeContactComputeTransport.hpp"
#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/HeatDisplayController.hpp"
#include "runtime/RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimeModelComputeTransport.hpp"
#include "runtime/RuntimePointComputeTransport.hpp"
#include "runtime/RuntimePointDisplayTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeVoronoiComputeTransport.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"
#include "runtime/VoronoiDisplayController.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "voronoi/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "app/ViewportTarget.hpp"
#include "render/RenderRuntime.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

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
        const AppVulkanContext& vulkanContext,
        std::atomic<bool>& runtimeBusy,
        std::atomic<bool>& isShuttingDown);
    bool initializeInputPipeline(SceneContext& scene);
    bool initializeSyncObjects();
    void shutdown();
    bool isInitialized() const;

    bool updateViewportTarget(VkImage image, VkFormat format, VkExtent2D extent);
    FrameSync& sync();
    RenderRuntime* runtime();
    const RenderRuntime* runtime() const;
    HeatSystemComputeController* heatSystemComputeController();
    const HeatSystemComputeController* heatSystemComputeController() const;
    ContactSystemComputeController* contactSystemComputeController();
    ModelComputeController* modelComputeController();
    const ModelComputeController* modelComputeController() const;
    SceneController* sceneController();
    const SceneController* sceneController() const;
    NodeGraphController* nodeGraphController();
    const NodeGraphController* nodeGraphController() const;
    InputController* inputController();

private:
    const WindowRuntimeState* windowState = nullptr;
    ViewportTarget viewportTarget;
    std::unique_ptr<RenderRuntime> renderRuntime;
    FrameSync frameSync;
    std::unique_ptr<RuntimeContactComputeTransport> runtimeContactComputeTransportState;
    std::unique_ptr<RuntimeContactDisplayTransport> runtimeContactDisplayTransportState;
    std::unique_ptr<RuntimeHeatComputeTransport> runtimeHeatComputeTransportState;
    std::unique_ptr<RuntimeHeatDisplayTransport> runtimeHeatDisplayTransportState;
    std::unique_ptr<RuntimeModelComputeTransport> runtimeModelComputeTransportState;
    std::unique_ptr<RuntimeModelDisplayTransport> runtimeModelDisplayTransportState;
    std::unique_ptr<RuntimePointComputeTransport> runtimePointComputeTransportState;
    std::unique_ptr<RuntimePointDisplayTransport> runtimePointDisplayTransportState;
    std::unique_ptr<RuntimeRemeshDisplayTransport> runtimeRemeshDisplayTransportState;
    std::unique_ptr<RuntimeRemeshComputeTransport> runtimeRemeshTransportState;
    std::unique_ptr<RuntimeVoronoiComputeTransport> runtimeVoronoiComputeTransportState;
    std::unique_ptr<RuntimeVoronoiDisplayTransport> runtimeVoronoiDisplayTransportState;
    std::unique_ptr<ModelComputeController> modelComputeControllerState;
    std::unique_ptr<ModelDisplayController> modelDisplayControllerState;
    std::unique_ptr<PointComputeRuntime> pointComputeRuntimeState;
    std::unique_ptr<PointDisplayController> pointDisplayControllerState;
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
    std::unique_ptr<NodeGraphController> nodeGraphControllerState;
    std::unique_ptr<NodePayloadRegistry> payloadRegistryState;
    bool initialized = false;
    bool inputPipelineInitialized = false;
};
