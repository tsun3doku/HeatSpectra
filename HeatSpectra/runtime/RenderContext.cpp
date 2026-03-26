#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RuntimeSimulationController.hpp"
#include "app/SwapchainManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "contact/ContactSystemController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "heat/VoronoiSystemController.hpp"
#include "mesh/MeshModifiers.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "runtime/ComputeCache.hpp"
#include "runtime/RuntimePayloadController.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

RenderContext::~RenderContext() = default;

bool RenderContext::initialize(VulkanCoreContext& core, SceneContext& scene, WindowRuntimeState& windowState,
    std::atomic<bool>& runtimeBusy, std::atomic<bool>& isShuttingDown) {
    if (initialized) {
        return true;
    }

    auto* allocator = core.allocator();
    auto* commandPool = core.commandPool();
    auto* resourceManager = scene.resourceManager();
    auto* meshModifiers = scene.meshModifiers();
    auto* modelUploader = scene.modelUploader();
    auto* uniformBufferManager = scene.uniformBufferManager();
    if (!allocator || !commandPool || !resourceManager || !meshModifiers || !modelUploader || !uniformBufferManager) {
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

    intrinsicRegistryState = std::make_unique<RuntimeIntrinsicCache>(core.device(), *allocator);
    if (!renderRuntime->initializeBase(
        swapChainImageFormat,
        swapChainExtent,
        *allocator,
        *intrinsicRegistryState,
        *resourceManager,
        *uniformBufferManager)) {
        shutdown();
        return false;
    }

    const VkRenderPass renderPass = renderRuntime->getFrameGraphRuntime().getRenderPass();

    computeCacheState = std::make_unique<ComputeCache>();
    payloadRegistryState = std::make_unique<NodePayloadRegistry>();
    runtimePayloadControllerState = std::make_unique<RuntimePayloadController>(
        core.device(),
        swapchainManager,
        *resourceManager,
        *intrinsicRegistryState,
        *renderRuntime,
        runtimeBusy);
    contactSystemControllerState = std::make_unique<ContactSystemController>(
        *computeCacheState);
    contactPreviewStoreState = std::make_unique<ContactPreviewStore>(core.device(), *allocator, *uniformBufferManager);
    contactPreviewStoreState->initRenderer(renderPass, renderconfig::MaxFramesInFlight);

    voronoiSystemControllerState = std::make_unique<VoronoiSystemController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *intrinsicRegistryState,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    voronoiSystemControllerState->createVoronoiSystem(swapChainExtent, renderPass);

    heatSystemControllerState = std::make_unique<HeatSystemController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *intrinsicRegistryState,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    runtimePayloadControllerState->setHeatSystemController(heatSystemControllerState.get());
    runtimePayloadControllerState->setContactSystemController(contactSystemControllerState.get());
    runtimePayloadControllerState->setVoronoiSystemController(voronoiSystemControllerState.get());
    heatSystemControllerState->setContactPreviewStore(contactPreviewStoreState.get());
    heatSystemControllerState->createHeatSystem(swapChainExtent, renderPass);

    sceneControllerState = std::make_unique<SceneController>(
        core.device(),
        *resourceManager,
        *modelUploader,
        *runtimePayloadControllerState,
        frameSync,
        scene.cameraController(),
        runtimeBusy);
    runtimePayloadControllerState->setSceneController(sceneControllerState.get());

    sceneControllerState->focusOnVisibleModel();

    NodeRuntimeServices nodeRuntimeServices{};
    nodeRuntimeServices.sceneController = sceneControllerState.get();
    nodeRuntimeServices.runtimePayloadController = runtimePayloadControllerState.get();
    nodeRuntimeServices.heatSystemController = heatSystemControllerState.get();
    nodeRuntimeServices.contactSystemController = contactSystemControllerState.get();
    nodeRuntimeServices.contactPreviewStore = contactPreviewStoreState.get();
    nodeRuntimeServices.payloadRegistry = payloadRegistryState.get();
    nodeRuntimeServices.resourceManager = resourceManager;
    nodeRuntimeServices.meshModifiers = meshModifiers;
    nodeRuntimeServices.remesher = &meshModifiers->getRemesher();

    nodeGraphBridgeState = std::make_unique<NodeGraphBridge>();
    NodeGraphEditor defaultGraphEditor(*nodeGraphBridgeState);
    defaultGraphEditor.resetToDefaultGraph();
    nodeGraphControllerState = std::make_unique<NodeGraphController>(nodeGraphBridgeState.get(), nodeRuntimeServices);

    initialized = true;
    return true;
}

bool RenderContext::initializeInputPipeline(SceneContext& scene, RuntimeSimulationController& simulationController) {
    if (!initialized || !renderRuntime || !sceneControllerState || !nodeGraphControllerState || !nodeGraphBridgeState) {
        return false;
    }
    if (inputPipelineInitialized) {
        return true;
    }

    auto* resourceManager = scene.resourceManager();
    auto* meshModifiers = scene.meshModifiers();
    auto* uniformBufferManager = scene.uniformBufferManager();
    auto* lightingSystem = scene.lightingSystem();
    auto* materialSystem = scene.materialSystem();
    if (!resourceManager || !meshModifiers || !uniformBufferManager || !lightingSystem || !materialSystem) {
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
        simulationController);

    RenderRuntimeServices renderRuntimeServices{
        *resourceManager,
        *meshModifiers,
        *uniformBufferManager,
        heatSystemControllerState->getHeatSystem(),
        voronoiSystemControllerState->getVoronoiSystem(),
        *inputControllerState,
        *lightingSystem,
        *materialSystem};
    if (!renderRuntime->initializeFrameController(renderRuntimeServices)) {
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
    sceneControllerState.reset();
    runtimePayloadControllerState.reset();
    contactSystemControllerState.reset();
    contactPreviewStoreState.reset();
    voronoiSystemControllerState.reset();
    computeCacheState.reset();
    intrinsicRegistryState.reset();
    heatSystemControllerState.reset();

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

HeatSystem* RenderContext::heatSystem() {
    if (!heatSystemControllerState) {
        return nullptr;
    }
    return heatSystemControllerState->getHeatSystem();
}

VoronoiSystem* RenderContext::voronoiSystem() {
    if (!voronoiSystemControllerState) {
        return nullptr;
    }
    return voronoiSystemControllerState->getVoronoiSystem();
}

HeatSystemController* RenderContext::heatSystemController() {
    return heatSystemControllerState.get();
}

ContactSystemController* RenderContext::contactSystemController() {
    return contactSystemControllerState.get();
}

ContactPreviewStore* RenderContext::contactPreviewStore() {
    return contactPreviewStoreState.get();
}

RuntimePayloadController* RenderContext::runtimePayloadController() {
    return runtimePayloadControllerState.get();
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
