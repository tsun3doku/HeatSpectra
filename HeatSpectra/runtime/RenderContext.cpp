#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RuntimeSimulationController.hpp"
#include "RenderSettingsController.hpp"
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
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/ModelRuntime.hpp"
#include "runtime/RemeshController.hpp"
#include "runtime/RuntimePackageController.hpp"
#include "runtime/RuntimeRemeshTransport.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"
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

    payloadRegistryState = std::make_unique<NodePayloadRegistry>();
    nodeGraphRuntimeBridgeState = std::make_unique<NodeGraphRuntimeBridge>();
    runtimeProductRegistryState = std::make_unique<RuntimeProductRegistry>();
    runtimeModelTransportState = std::make_unique<RuntimeModelTransport>();
    modelRuntimeState = std::make_unique<ModelRuntime>(
        core.device(),
        *resourceManager,
        *modelUploader,
        frameSync,
        runtimeBusy);
    runtimePackageControllerState = std::make_unique<RuntimePackageController>(*modelRuntimeState);
    runtimeRemeshTransportState = std::make_unique<RuntimeRemeshTransport>();
    remeshControllerState = std::make_unique<RemeshController>(
        meshModifiers->getRemesher(),
        core.device(),
        *modelRuntimeState,
        *resourceManager,
        *renderRuntime,
        runtimeBusy);
    runtimeContactTransportState = std::make_unique<RuntimeContactTransport>();
    contactSystemControllerState = std::make_unique<ContactSystemController>(
        core.device(),
        *allocator,
        *uniformBufferManager,
        renderconfig::MaxFramesInFlight);
    contactSystemControllerState->createContactSystem(swapChainExtent, renderPass);
    runtimeContactTransportState->setController(contactSystemControllerState.get());
    runtimeContactTransportState->setProductRegistry(runtimeProductRegistryState.get());

    runtimeVoronoiTransportState = std::make_unique<RuntimeVoronoiTransport>();
    voronoiSystemControllerState = std::make_unique<VoronoiSystemController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    voronoiSystemControllerState->createVoronoiSystem(swapChainExtent, renderPass);
    runtimeVoronoiTransportState->setController(voronoiSystemControllerState.get());
    runtimeVoronoiTransportState->setProductRegistry(runtimeProductRegistryState.get());

    runtimeHeatTransportState = std::make_unique<RuntimeHeatTransport>();
    heatSystemControllerState = std::make_unique<HeatSystemController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    runtimeHeatTransportState->setController(heatSystemControllerState.get());
    runtimeHeatTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeModelTransportState->setRuntime(modelRuntimeState.get());
    runtimeModelTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeRemeshTransportState->setController(remeshControllerState.get());
    runtimeRemeshTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeRemeshTransportState->setModelTransport(runtimeModelTransportState.get());
    runtimePackageControllerState->setModelTransport(runtimeModelTransportState.get());
    runtimePackageControllerState->setRemeshTransport(runtimeRemeshTransportState.get());
    runtimePackageControllerState->setHeatTransport(runtimeHeatTransportState.get());
    runtimePackageControllerState->setContactTransport(runtimeContactTransportState.get());
    runtimePackageControllerState->setVoronoiTransport(runtimeVoronoiTransportState.get());
    heatSystemControllerState->createHeatSystem(swapChainExtent, renderPass);

    sceneControllerState = std::make_unique<SceneController>(
        core.device(),
        *resourceManager,
        *modelUploader,
        frameSync,
        scene.cameraController(),
        runtimeBusy);
    sceneControllerState->setModelRuntime(modelRuntimeState.get());
    modelRuntimeState->setSceneController(sceneControllerState.get());

    sceneControllerState->focusOnVisibleModel();

    NodeRuntimeServices nodeRuntimeServices{};
    nodeRuntimeServices.sceneController = sceneControllerState.get();
    nodeRuntimeServices.runtimePackageController = runtimePackageControllerState.get();
    nodeRuntimeServices.heatSystemController = heatSystemControllerState.get();
    nodeRuntimeServices.contactSystemController = contactSystemControllerState.get();
    nodeRuntimeServices.renderSettingsController = renderSettingsController;
    nodeRuntimeServices.payloadRegistry = payloadRegistryState.get();
    nodeRuntimeServices.runtimeBridge = nodeGraphRuntimeBridgeState.get();
    nodeRuntimeServices.runtimeProductRegistry = runtimeProductRegistryState.get();
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
        heatSystemControllerState.get(),
        contactSystemControllerState.get(),
        voronoiSystemControllerState.get(),
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
    nodeGraphRuntimeBridgeState.reset();
    sceneControllerState.reset();
    runtimePackageControllerState.reset();
    runtimeModelTransportState.reset();
    runtimeRemeshTransportState.reset();
    remeshControllerState.reset();
    runtimeContactTransportState.reset();
    runtimeHeatTransportState.reset();
    contactSystemControllerState.reset();
    runtimeVoronoiTransportState.reset();
    runtimeProductRegistryState.reset();
    voronoiSystemControllerState.reset();
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

HeatSystemController* RenderContext::heatSystemController() {
    return heatSystemControllerState.get();
}

ContactSystemController* RenderContext::contactSystemController() {
    return contactSystemControllerState.get();
}

ModelRuntime* RenderContext::modelRuntime() {
    return modelRuntimeState.get();
}

const ModelRuntime* RenderContext::modelRuntime() const {
    return modelRuntimeState.get();
}

RuntimePackageController* RenderContext::runtimePackageController() {
    return runtimePackageControllerState.get();
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
