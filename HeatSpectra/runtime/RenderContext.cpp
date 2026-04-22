#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RenderSettingsController.hpp"
#include "app/SwapchainManager.hpp"
#include "framegraph/FrameController.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "contact/ContactSystemComputeController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "heat/VoronoiSystemComputeController.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/ModelComputeRuntime.hpp"
#include "runtime/ModelDisplayRuntime.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/RemeshDisplayController.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "render/SceneRenderer.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/SceneController.hpp"

RenderContext::~RenderContext() = default;

bool RenderContext::initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState, RenderSettingsController* renderSettingsController,
    std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown) {
    if (initialized) {
        return true;
    }

    auto* allocator = core.allocator();
    auto* commandPool = core.commandPool();
    auto* resourceManager = scene.resourceManager();
    auto* modelUploader = scene.modelUploader();
    auto* uniformBufferManager = scene.uniformBufferManager();
    if (!allocator || !commandPool || !resourceManager || !modelUploader || !uniformBufferManager) {
        return false;
    }

    swapchainManager.initialize(core.device(), windowState);
    if (!swapchainManager.create()) {
        return false;
    }

    const VkFormat swapChainImageFormat = swapchainManager.getImageFormat();
    const VkExtent2D swapChainExtent = swapchainManager.getExtent();

    renderRuntime = std::make_unique<RenderRuntime>(
        windowState,
        core.device(),
        swapchainManager,
        *commandPool,
        frameSync,
        scene.cameraController(),
        runtimeBusy,
        isShuttingDown);

    if (!renderRuntime->initializeBase(
        swapChainImageFormat,
        swapChainExtent,
        *allocator,
        *resourceManager,
        *uniformBufferManager)) {
        shutdown();
        return false;
    }

    const VkRenderPass renderPass = renderRuntime->getFrameGraphRuntime().getRenderPass();

    payloadRegistryState = std::make_unique<NodePayloadRegistry>();
    nodeGraphRuntimeBridgeState = std::make_unique<NodeGraphRuntimeBridge>();
    runtimeModelComputeTransportState = std::make_unique<RuntimeModelComputeTransport>();
    runtimeModelDisplayTransportState = std::make_unique<RuntimeModelDisplayTransport>();
    modelComputeRuntimeState = std::make_unique<ModelComputeRuntime>(
        core.device(),
        *resourceManager,
        *modelUploader,
        frameSync,
        runtimeBusy);
    modelDisplayRuntimeState = std::make_unique<ModelDisplayRuntime>(*resourceManager);
    runtimeRemeshDisplayTransportState = std::make_unique<RuntimeRemeshDisplayTransport>();
    runtimeRemeshTransportState = std::make_unique<RuntimeRemeshComputeTransport>();
    sceneControllerState = std::make_unique<SceneController>(
        core.device(),
        *resourceManager,
        *modelUploader,
        frameSync,
        scene.cameraController(),
        runtimeBusy);
    sceneControllerState->setModelComputeRuntime(modelComputeRuntimeState.get());
    remeshControllerState = std::make_unique<RemeshController>(
        core.device(),
        *allocator,
        *resourceManager,
        runtimeBusy);
    remeshDisplayControllerState = std::make_unique<RemeshDisplayController>();
    contactDisplayControllerState = std::make_unique<ContactDisplayController>();
    runtimeContactDisplayTransportState = std::make_unique<RuntimeContactDisplayTransport>();
    runtimeContactComputeTransportState = std::make_unique<RuntimeContactComputeTransport>();
    contactSystemComputeControllerState = std::make_unique<ContactSystemComputeController>(
        core.device(),
        *allocator);
    runtimeContactComputeTransportState->setController(contactSystemComputeControllerState.get());
    contactDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getContactOverlayRenderer());
    runtimeContactDisplayTransportState->setController(contactDisplayControllerState.get());

    voronoiDisplayControllerState = std::make_unique<VoronoiDisplayController>();
    runtimeVoronoiDisplayTransportState = std::make_unique<RuntimeVoronoiDisplayTransport>();
    runtimeVoronoiComputeTransportState = std::make_unique<RuntimeVoronoiComputeTransport>();
    voronoiSystemComputeControllerState = std::make_unique<VoronoiSystemComputeController>(
        core.device(),
        *allocator,
        *resourceManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    runtimeVoronoiComputeTransportState->setController(voronoiSystemComputeControllerState.get());
    voronoiDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getVoronoiOverlayRenderer());
    runtimeVoronoiDisplayTransportState->setController(voronoiDisplayControllerState.get());

    heatDisplayControllerState = std::make_unique<HeatDisplayController>();
    runtimeHeatComputeTransportState = std::make_unique<RuntimeHeatComputeTransport>();
    runtimeHeatDisplayTransportState = std::make_unique<RuntimeHeatDisplayTransport>();
    heatSystemComputeControllerState = std::make_unique<HeatSystemComputeController>(
        core.device(),
        *allocator,
        *resourceManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    heatDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getHeatOverlayRenderer());
    runtimeHeatComputeTransportState->setController(heatSystemComputeControllerState.get());
    runtimeHeatDisplayTransportState->setController(heatDisplayControllerState.get());
    runtimeModelComputeTransportState->setRuntime(modelComputeRuntimeState.get());
    runtimeModelDisplayTransportState->setRuntime(modelDisplayRuntimeState.get());
    runtimeRemeshTransportState->setController(remeshControllerState.get());
    remeshDisplayControllerState->setIntrinsicRenderer(renderRuntime->getSceneRenderer().getIntrinsicRenderer());
    runtimeRemeshDisplayTransportState->setController(remeshDisplayControllerState.get());

    modelDisplayRuntimeState->setComputeRuntime(modelComputeRuntimeState.get());

    sceneControllerState->focusOnVisibleModel();

    NodeRuntimeServices nodeRuntimeServices{};
    nodeRuntimeServices.sceneController = sceneControllerState.get();
    nodeRuntimeServices.modelComputeTransport = runtimeModelComputeTransportState.get();
    nodeRuntimeServices.remeshComputeTransport = runtimeRemeshTransportState.get();
    nodeRuntimeServices.voronoiComputeTransport = runtimeVoronoiComputeTransportState.get();
    nodeRuntimeServices.contactComputeTransport = runtimeContactComputeTransportState.get();
    nodeRuntimeServices.heatComputeTransport = runtimeHeatComputeTransportState.get();
    nodeRuntimeServices.modelDisplayTransport = runtimeModelDisplayTransportState.get();
    nodeRuntimeServices.remeshDisplayTransport = runtimeRemeshDisplayTransportState.get();
    nodeRuntimeServices.voronoiDisplayTransport = runtimeVoronoiDisplayTransportState.get();
    nodeRuntimeServices.contactDisplayTransport = runtimeContactDisplayTransportState.get();
    nodeRuntimeServices.heatDisplayTransport = runtimeHeatDisplayTransportState.get();
    nodeRuntimeServices.heatSystemController = heatSystemComputeControllerState.get();
    nodeRuntimeServices.renderSettingsController = renderSettingsController;
    nodeRuntimeServices.payloadRegistry = payloadRegistryState.get();
    nodeRuntimeServices.runtimeBridge = nodeGraphRuntimeBridgeState.get();
    nodeRuntimeServices.resourceManager = resourceManager;
    nodeRuntimeServices.remesher = &remeshControllerState->getRemesher();

    nodeGraphBridgeState = std::make_unique<NodeGraphBridge>();
    NodeGraphEditor defaultGraphEditor(*nodeGraphBridgeState);
    defaultGraphEditor.resetToDefaultGraph();
    nodeGraphControllerState = std::make_unique<NodeGraphController>(nodeGraphBridgeState.get(), nodeRuntimeServices);

    initialized = true;
    return true;
}

bool RenderContext::initializeInputPipeline(SceneContext& scene, InputActionHandler& inputActions) {
    if (!initialized || !renderRuntime || !sceneControllerState || !nodeGraphControllerState || !nodeGraphBridgeState) {
        return false;
    }
    if (inputPipelineInitialized) {
        return true;
    }

    auto* resourceManager = scene.resourceManager();
    auto* uniformBufferManager = scene.uniformBufferManager();
    auto* lightingSystem = scene.lightingSystem();
    auto* materialSystem = scene.materialSystem();
    if (!resourceManager || !uniformBufferManager || !lightingSystem || !materialSystem) {
        return false;
    }

    inputControllerState = std::make_unique<InputController>(
        scene.cameraController().getCamera(),
        renderRuntime->getGizmoController(),
        renderRuntime->getModelSelection(),
        *resourceManager,
        *sceneControllerState,
        *nodeGraphBridgeState,
        swapchainManager,
        inputActions);

    FrameControllerServices frameControllerServices{
        *resourceManager,
        *uniformBufferManager,
        renderRuntime->getModelSelection(),
        renderRuntime->getGizmoController(),
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
    if (renderRuntime) {
        return renderRuntime->initializeSyncObjects();
    }
    return false;
}

void RenderContext::shutdown() {
    inputControllerState.reset();
    nodeGraphControllerState.reset();
    nodeGraphBridgeState.reset();
    nodeGraphRuntimeBridgeState.reset();
    sceneControllerState.reset();
    runtimeModelDisplayTransportState.reset();
    runtimeRemeshDisplayTransportState.reset();
    runtimeContactDisplayTransportState.reset();
    runtimeRemeshTransportState.reset();
    remeshControllerState.reset();
    remeshDisplayControllerState.reset();
    contactDisplayControllerState.reset();
    runtimeContactComputeTransportState.reset();
    runtimeHeatComputeTransportState.reset();
    runtimeHeatDisplayTransportState.reset();
    contactSystemComputeControllerState.reset();
    runtimeVoronoiDisplayTransportState.reset();
    runtimeVoronoiComputeTransportState.reset();
    runtimeModelComputeTransportState.reset();
    modelDisplayRuntimeState.reset();
    modelComputeRuntimeState.reset();
    voronoiDisplayControllerState.reset();
    voronoiSystemComputeControllerState.reset();
    heatSystemComputeControllerState.reset();
    heatDisplayControllerState.reset();

    if (renderRuntime) {
        renderRuntime->cleanupSwapChain();
        renderRuntime->shutdownSyncObjects();
        renderRuntime->cleanup();
    } else {
        frameSync.shutdown();
    }
    renderRuntime.reset();
    swapchainManager.cleanup();
    inputPipelineInitialized = false;
    initialized = false;
}

bool RenderContext::isInitialized() const {
    return initialized;
}

SwapchainManager& RenderContext::swapchain() {
    return swapchainManager;
}

const SwapchainManager& RenderContext::swapchain() const {
    return swapchainManager;
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

ContactSystemComputeController* RenderContext::contactSystemComputeController() {
    return contactSystemComputeControllerState.get();
}

ModelComputeRuntime* RenderContext::modelComputeRuntime() {
    return modelComputeRuntimeState.get();
}

const ModelComputeRuntime* RenderContext::modelComputeRuntime() const {
    return modelComputeRuntimeState.get();
}

SceneController* RenderContext::sceneController() {
    return sceneControllerState.get();
}

const SceneController* RenderContext::sceneController() const {
    return sceneControllerState.get();
}

NodeGraphBridge* RenderContext::nodeGraphBridge() {
    return nodeGraphBridgeState.get();
}

const NodeGraphBridge* RenderContext::nodeGraphBridge() const {
    return nodeGraphBridgeState.get();
}

NodeGraphController* RenderContext::nodeGraphController() {
    return nodeGraphControllerState.get();
}

InputController* RenderContext::inputController() {
    return inputControllerState.get();
}
