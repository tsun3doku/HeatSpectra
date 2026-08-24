#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "app/ViewportTarget.hpp"
#include "framegraph/FrameController.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "voronoi/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/ModelComputeController.hpp"
#include "runtime/PointComputeRuntime.hpp"
#include "runtime/PointDisplayController.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "runtime/RuntimePointComputeTransport.hpp"
#include "runtime/RuntimePointDisplayTransport.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "render/SceneRenderer.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/IBLSystem.hpp"
#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/SceneController.hpp"

RenderContext::~RenderContext() = default;

bool RenderContext::initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState, const AppVulkanContext& vulkanContext,
    std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown) {
    if (initialized) {
        return true;
    }

    this->windowState = &windowState;

    auto* allocator = core.allocator();
    auto* commandPool = core.getRenderCommandPool();
    auto* transferCommandPool = core.getTransferCommandPool();
    auto* modelRegistry = scene.modelRegistry();
    auto* modelUploader = scene.modelUploader();
    auto* uniformBufferManager = scene.uniformBufferManager();
    auto* iblSystem = scene.iblSystem();
    if (!allocator || !commandPool || !transferCommandPool || !modelRegistry || !modelUploader || !uniformBufferManager || !iblSystem) {
        return false;
    }

    viewportTarget.initialize(core.device());
    if (!viewportTarget.update(
            vulkanContext.viewportImage,
            vulkanContext.viewportFormat,
            vulkanContext.viewportExtent)) {
        return false;
    }
    if (!frameSync.initialize(core.device().getDevice(), renderconfig::MaxFramesInFlight)) {
        return false;
    }

    const VkFormat viewportImageFormat = viewportTarget.getImageFormat();
    const VkExtent2D viewportExtent = viewportTarget.getExtent();

    renderRuntime = std::make_unique<RenderRuntime>(
        windowState,
        core.device(),
        viewportTarget,
        *commandPool,
        frameSync,
        scene.cameraController(),
        runtimeBusy,
        isShuttingDown);

    if (!renderRuntime->initializeBase(
        viewportImageFormat,
        viewportExtent,
        *allocator,
        *modelRegistry,
        *uniformBufferManager,
        *iblSystem)) {
        shutdown();
        return false;
    }

    payloadRegistryState = std::make_unique<NodePayloadRegistry>();
    runtimeModelComputeTransportState = std::make_unique<RuntimeModelComputeTransport>();
    runtimeModelDisplayTransportState = std::make_unique<RuntimeModelDisplayTransport>();
    runtimePointComputeTransportState = std::make_unique<RuntimePointComputeTransport>();
    runtimePointDisplayTransportState = std::make_unique<RuntimePointDisplayTransport>();
    pointDisplayControllerState = std::make_unique<PointDisplayController>();
    pointComputeRuntimeState = std::make_unique<PointComputeRuntime>(
        core.device(),
        *allocator,
        *commandPool);
    modelDisplayControllerState = std::make_unique<ModelDisplayController>();
    modelComputeControllerState = std::make_unique<ModelComputeController>(
        core.device(),
        *modelRegistry,
        *modelUploader,
        frameSync,
        runtimeBusy);
    runtimeRemeshDisplayTransportState = std::make_unique<RuntimeRemeshDisplayTransport>();
    runtimeRemeshTransportState = std::make_unique<RuntimeRemeshComputeTransport>();
    sceneControllerState = std::make_unique<SceneController>(
        core.device(),
        *modelRegistry,
        *modelUploader,
        frameSync,
        scene.cameraController(),
        runtimeBusy);
    sceneControllerState->setModelComputeController(modelComputeControllerState.get());
    remeshControllerState = std::make_unique<RemeshController>(
        core.device(),
        *allocator,
        *modelRegistry,
        runtimeBusy);
    remeshDisplayControllerState = std::make_unique<RemeshDisplayController>();

    voronoiDisplayControllerState = std::make_unique<VoronoiDisplayController>();
    runtimeVoronoiDisplayTransportState = std::make_unique<RuntimeVoronoiDisplayTransport>();
    runtimeVoronoiComputeTransportState = std::make_unique<RuntimeVoronoiComputeTransport>();
    voronoiSystemComputeControllerState = std::make_unique<VoronoiSystemComputeController>(
        core.device(),
        *allocator,
        *transferCommandPool);
    runtimeVoronoiComputeTransportState->setController(voronoiSystemComputeControllerState.get());
    voronoiDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getVoronoiOverlayRenderer());
    runtimeVoronoiDisplayTransportState->setController(voronoiDisplayControllerState.get());

    heatDisplayControllerState = std::make_unique<HeatDisplayController>();
    runtimeHeatComputeTransportState = std::make_unique<RuntimeHeatComputeTransport>();
    runtimeHeatDisplayTransportState = std::make_unique<RuntimeHeatDisplayTransport>();
    heatSystemComputeControllerState = std::make_unique<HeatSystemComputeController>(
        core.device(),
        *allocator,
        *commandPool,
        *transferCommandPool,
        renderconfig::MaxFramesInFlight);
    heatDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getHeatOverlayRenderer());
    runtimeHeatComputeTransportState->setController(heatSystemComputeControllerState.get());
    runtimeHeatDisplayTransportState->setController(heatDisplayControllerState.get());
    runtimePointComputeTransportState->setRuntime(pointComputeRuntimeState.get());
    pointDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getPointOverlayRenderer());
    runtimePointDisplayTransportState->setController(pointDisplayControllerState.get());
    runtimeModelComputeTransportState->setController(modelComputeControllerState.get());
    modelDisplayControllerState->setModelRegistry(modelRegistry);
    runtimeModelDisplayTransportState->setController(modelDisplayControllerState.get());
    runtimeRemeshTransportState->setController(remeshControllerState.get());
    remeshDisplayControllerState->setIntrinsicRenderer(renderRuntime->getSceneRenderer().getIntrinsicRenderer());
    runtimeRemeshDisplayTransportState->setController(remeshDisplayControllerState.get());

    sceneControllerState->focusOnVisibleModel();

    RuntimeConnections runtimeConnections{};
    runtimeConnections.modelComputeTransport = runtimeModelComputeTransportState.get();
    runtimeConnections.pointComputeTransport = runtimePointComputeTransportState.get();
    runtimeConnections.remeshComputeTransport = runtimeRemeshTransportState.get();
    runtimeConnections.voronoiComputeTransport = runtimeVoronoiComputeTransportState.get();
    runtimeConnections.heatComputeTransport = runtimeHeatComputeTransportState.get();
    runtimeConnections.modelDisplayTransport = runtimeModelDisplayTransportState.get();
    runtimeConnections.pointDisplayTransport = runtimePointDisplayTransportState.get();
    runtimeConnections.remeshDisplayTransport = runtimeRemeshDisplayTransportState.get();
    runtimeConnections.voronoiDisplayTransport = runtimeVoronoiDisplayTransportState.get();
    runtimeConnections.heatDisplayTransport = runtimeHeatDisplayTransportState.get();
    nodeGraphControllerState = std::make_unique<NodeGraphController>(
        *payloadRegistryState, runtimeConnections, core.device(), *allocator);

    initialized = true;
    return true;
}

bool RenderContext::initializeInputPipeline(SceneContext& scene) {
    if (!initialized || !renderRuntime || !sceneControllerState || !nodeGraphControllerState) {
        return false;
    }
    if (inputPipelineInitialized) {
        return true;
    }

    auto* modelRegistry = scene.modelRegistry();
    auto* uniformBufferManager = scene.uniformBufferManager();
    auto* lightingSystem = scene.lightingSystem();
    auto* materialSystem = scene.materialSystem();
    if (!modelRegistry || !uniformBufferManager || !lightingSystem || !materialSystem) {
        return false;
    }

    inputControllerState = std::make_unique<InputController>(
        scene.cameraController(),
        renderRuntime->getGizmoController(),
        renderRuntime->getNavigationGizmoController(),
        renderRuntime->getModelSelection(),
        *modelRegistry,
        *windowState);

    FrameControllerServices frameControllerServices{
        *modelRegistry,
        *uniformBufferManager,
        renderRuntime->getModelSelection(),
        renderRuntime->getGizmoController(),
        renderRuntime->getNavigationGizmoController(),
        renderRuntime->getWireframeRenderer(),
        *inputControllerState,
        *lightingSystem,
        *materialSystem};
    if (!renderRuntime->initializeFrameController(frameControllerServices)) {
        inputControllerState.reset();
        inputPipelineInitialized = false;
        return false;
    }

    inputPipelineInitialized = true;
    return true;
}

bool RenderContext::initializeSyncObjects() {
    return initialized;
}

void RenderContext::shutdown() {
    frameSync.waitForAllFrameFences();

    if (nodeGraphControllerState && nodeGraphControllerState->getProductManager()) {
        nodeGraphControllerState->getProductManager()->destroyAll();
    }
    inputControllerState.reset();
    nodeGraphControllerState.reset();
    sceneControllerState.reset();
    runtimeModelDisplayTransportState.reset();
    runtimeRemeshDisplayTransportState.reset();
    runtimeRemeshTransportState.reset();
    remeshControllerState.reset();
    remeshDisplayControllerState.reset();
    runtimeHeatComputeTransportState.reset();
    runtimeHeatDisplayTransportState.reset();
    runtimeVoronoiDisplayTransportState.reset();
    runtimeVoronoiComputeTransportState.reset();
    runtimeModelComputeTransportState.reset();
    modelComputeControllerState.reset();
    voronoiDisplayControllerState.reset();
    voronoiSystemComputeControllerState.reset();
    heatSystemComputeControllerState.reset();
    heatDisplayControllerState.reset();

    if (renderRuntime) {
        renderRuntime->cleanup();
    }
    renderRuntime.reset();
    frameSync.shutdown();
    viewportTarget.cleanup();
    windowState = nullptr;
    inputPipelineInitialized = false;
    initialized = false;
}

bool RenderContext::isInitialized() const {
    return initialized;
}

bool RenderContext::updateViewportTarget(VkImage image, VkFormat format, VkExtent2D extent) {
    return renderRuntime && renderRuntime->updateViewportTarget(image, format, extent);
}

FrameSync& RenderContext::sync() {
    return frameSync;
}

RenderRuntime* RenderContext::runtime() {
    return renderRuntime.get();
}

const RenderRuntime* RenderContext::runtime() const {
    return renderRuntime.get();
}

HeatSystemComputeController* RenderContext::heatSystemComputeController() {
    return heatSystemComputeControllerState.get();
}

const HeatSystemComputeController* RenderContext::heatSystemComputeController() const {
    return heatSystemComputeControllerState.get();
}

ModelComputeController* RenderContext::modelComputeController() {
    return modelComputeControllerState.get();
}

const ModelComputeController* RenderContext::modelComputeController() const {
    return modelComputeControllerState.get();
}

SceneController* RenderContext::sceneController() {
    return sceneControllerState.get();
}

const SceneController* RenderContext::sceneController() const {
    return sceneControllerState.get();
}

NodeGraphController* RenderContext::nodeGraphController() {
    return nodeGraphControllerState.get();
}

const NodeGraphController* RenderContext::nodeGraphController() const {
    return nodeGraphControllerState.get();
}

InputController* RenderContext::inputController() {
    return inputControllerState.get();
}
