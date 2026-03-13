#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RuntimeSimulationController.hpp"
#include "app/SwapchainManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "heat/ContactSystemController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "mesh/MeshModifiers.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeSolverController.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"

RenderContext::~RenderContext() = default;

bool RenderContext::initialize(
    VulkanCoreContext& core,
    SceneContext& scene,
    WindowRuntimeState& windowState,
    std::atomic<bool>& runtimeBusy,
    std::atomic<bool>& isShuttingDown) {
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

    contactSystemControllerState = std::make_unique<ContactSystemController>(
        scene.modelRegistry(),
        core.device(),
        *allocator,
        *resourceManager,
        meshModifiers->getRemesher(),
        *uniformBufferManager,
        *commandPool);
    contactSystemControllerState->initRenderer(renderPass, renderconfig::MaxFramesInFlight);

    heatSystemControllerState = std::make_unique<HeatSystemController>(
        core.device(),
        *allocator,
        *resourceManager,
        *meshModifiers,
        *modelUploader,
        *uniformBufferManager,
        renderRuntime->getSceneRenderer(),
        swapchainManager,
        renderRuntime->getFrameGraphRuntime(),
        *commandPool,
        frameSync,
        heatSystemState,
        runtimeBusy,
        renderconfig::MaxFramesInFlight);
    heatSystemControllerState->setContactSystemController(contactSystemControllerState.get());
    heatSystemControllerState->createHeatSystem(swapChainExtent, renderPass);

    sceneControllerState = std::make_unique<SceneController>(
        core.device(),
        swapchainManager,
        *resourceManager,
        *meshModifiers,
        *renderRuntime,
        *heatSystemControllerState,
        scene.cameraController(),
        runtimeBusy);
    modelRegistry = &scene.modelRegistry();
    modelRegistry->setSceneController(sceneControllerState.get());

    nodeSolverController = std::make_unique<NodeSolverController>(
        scene.modelRegistry(),
        *heatSystemControllerState);
    sceneControllerState->focusOnVisibleModel();

    NodeRuntimeServices nodeRuntimeServices{};
    nodeRuntimeServices.modelRegistry = &scene.modelRegistry();
    nodeRuntimeServices.sceneController = sceneControllerState.get();
    nodeRuntimeServices.heatSystemController = heatSystemControllerState.get();
    nodeRuntimeServices.contactSystemController = contactSystemControllerState.get();
    nodeRuntimeServices.nodeSolverController = nodeSolverController.get();
    nodeRuntimeServices.resourceManager = resourceManager;
    nodeRuntimeServices.meshModifiers = meshModifiers;

    nodeGraphBridgeState = std::make_unique<NodeGraphBridge>();
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
        swapchainManager,
        simulationController);

    RenderRuntimeServices renderRuntimeServices{
        *resourceManager,
        *meshModifiers,
        *uniformBufferManager,
        heatSystemState.get(),
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
    nodeSolverController.reset();
    if (modelRegistry) {
        modelRegistry->setSceneController(nullptr);
    }
    sceneControllerState.reset();
    contactSystemControllerState.reset();
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

    if (heatSystemState) {
        heatSystemState->cleanupResources();
        heatSystemState->cleanup();
        heatSystemState.reset();
    }

    modelRegistry = nullptr;
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
    return heatSystemState.get();
}

HeatSystemController* RenderContext::heatSystemController() {
    return heatSystemControllerState.get();
}

ContactSystemController* RenderContext::contactSystemController() {
    return contactSystemControllerState.get();
}

SceneController* RenderContext::sceneController() {
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
