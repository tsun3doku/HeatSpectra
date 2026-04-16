#include "RenderContext.hpp"

#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RenderSettingsController.hpp"
#include "app/SwapchainManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "contact/ContactSystemComputeController.hpp"
#include "contact/ContactSystemDisplayController.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "heat/HeatSystemDisplayController.hpp"
#include "heat/VoronoiSystemComputeController.hpp"
#include "mesh/MeshModifiers.hpp"
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
#include "runtime/RuntimeComputePackageController.hpp"
#include "runtime/RuntimeDisplayPackageController.hpp"
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
    runtimeModelComputeTransportState = std::make_unique<RuntimeModelComputeTransport>();
    runtimeModelDisplayTransportState = std::make_unique<RuntimeModelDisplayTransport>();
    modelComputeRuntimeState = std::make_unique<ModelComputeRuntime>(
        core.device(),
        *resourceManager,
        *modelUploader,
        frameSync,
        runtimeBusy);
    modelDisplayRuntimeState = std::make_unique<ModelDisplayRuntime>(*resourceManager);
    runtimeComputePackageControllerState = std::make_unique<RuntimeComputePackageController>();
    runtimeDisplayPackageControllerState = std::make_unique<RuntimeDisplayPackageController>();
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
        meshModifiers->getRemesher(),
        core.device(),
        *resourceManager,
        runtimeBusy);
    remeshDisplayControllerState = std::make_unique<RemeshDisplayController>();
    contactDisplayControllerState = std::make_unique<ContactDisplayController>();
    contactSystemDisplayControllerState = std::make_unique<ContactSystemDisplayController>();
    runtimeContactDisplayTransportState = std::make_unique<RuntimeContactDisplayTransport>();
    runtimeContactComputeTransportState = std::make_unique<RuntimeContactComputeTransport>();
    contactSystemComputeControllerState = std::make_unique<ContactSystemComputeController>(
        core.device(),
        *allocator,
        *uniformBufferManager,
        renderconfig::MaxFramesInFlight);
    contactSystemComputeControllerState->createContactSystem(swapChainExtent, renderPass);
    runtimeContactComputeTransportState->setController(contactSystemComputeControllerState.get());
    runtimeContactComputeTransportState->setProductRegistry(runtimeProductRegistryState.get());
    contactSystemDisplayControllerState->setComputeController(contactSystemComputeControllerState.get());
    contactDisplayControllerState->setController(contactSystemDisplayControllerState.get());
    contactDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getContactOverlayRenderer());
    runtimeContactDisplayTransportState->setController(contactDisplayControllerState.get());
    runtimeContactDisplayTransportState->setProductRegistry(runtimeProductRegistryState.get());

    voronoiDisplayControllerState = std::make_unique<VoronoiDisplayController>();
    runtimeVoronoiDisplayTransportState = std::make_unique<RuntimeVoronoiDisplayTransport>();
    runtimeVoronoiComputeTransportState = std::make_unique<RuntimeVoronoiComputeTransport>();
    voronoiSystemComputeControllerState = std::make_unique<VoronoiSystemComputeController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    voronoiSystemComputeControllerState->createVoronoiSystem(swapChainExtent, renderPass);
    runtimeVoronoiComputeTransportState->setController(voronoiSystemComputeControllerState.get());
    runtimeVoronoiComputeTransportState->setProductRegistry(runtimeProductRegistryState.get());
    voronoiDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getVoronoiOverlayRenderer());
    runtimeVoronoiDisplayTransportState->setController(voronoiDisplayControllerState.get());
    runtimeVoronoiDisplayTransportState->setProductRegistry(runtimeProductRegistryState.get());

    heatDisplayControllerState = std::make_unique<HeatDisplayController>();
    heatSystemDisplayControllerState = std::make_unique<HeatSystemDisplayController>();
    runtimeHeatComputeTransportState = std::make_unique<RuntimeHeatComputeTransport>();
    runtimeHeatDisplayTransportState = std::make_unique<RuntimeHeatDisplayTransport>();
    heatSystemComputeControllerState = std::make_unique<HeatSystemComputeController>(
        core.device(),
        *allocator,
        *resourceManager,
        *uniformBufferManager,
        *commandPool,
        renderconfig::MaxFramesInFlight);
    heatSystemDisplayControllerState->setComputeController(heatSystemComputeControllerState.get());
    heatDisplayControllerState->setController(heatSystemDisplayControllerState.get());
    heatDisplayControllerState->setOverlayRenderer(renderRuntime->getSceneRenderer().getHeatOverlayRenderer());
    runtimeHeatComputeTransportState->setController(heatSystemComputeControllerState.get());
    runtimeHeatComputeTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeHeatDisplayTransportState->setController(heatDisplayControllerState.get());
    runtimeHeatDisplayTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeModelComputeTransportState->setRuntime(modelComputeRuntimeState.get());
    runtimeModelComputeTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeModelDisplayTransportState->setRuntime(modelDisplayRuntimeState.get());
    runtimeRemeshTransportState->setController(remeshControllerState.get());
    runtimeRemeshTransportState->setProductRegistry(runtimeProductRegistryState.get());
    remeshDisplayControllerState->setIntrinsicRenderer(renderRuntime->getSceneRenderer().getIntrinsicRenderer());
    runtimeRemeshDisplayTransportState->setController(remeshDisplayControllerState.get());
    runtimeRemeshDisplayTransportState->setProductRegistry(runtimeProductRegistryState.get());
    runtimeComputePackageControllerState->setModelTransport(runtimeModelComputeTransportState.get());
    runtimeDisplayPackageControllerState->setModelTransport(runtimeModelDisplayTransportState.get());
    runtimeDisplayPackageControllerState->setRemeshDisplayTransport(runtimeRemeshDisplayTransportState.get());
    runtimeDisplayPackageControllerState->setContactDisplayTransport(runtimeContactDisplayTransportState.get());
    runtimeComputePackageControllerState->setRemeshTransport(runtimeRemeshTransportState.get());
    runtimeDisplayPackageControllerState->setHeatDisplayTransport(runtimeHeatDisplayTransportState.get());
    runtimeComputePackageControllerState->setHeatTransport(runtimeHeatComputeTransportState.get());
    runtimeComputePackageControllerState->setContactComputeTransport(runtimeContactComputeTransportState.get());
    runtimeComputePackageControllerState->setVoronoiComputeTransport(runtimeVoronoiComputeTransportState.get());
    runtimeDisplayPackageControllerState->setVoronoiDisplayTransport(runtimeVoronoiDisplayTransportState.get());
    heatSystemComputeControllerState->createHeatSystem(swapChainExtent, renderPass);

    modelDisplayRuntimeState->setComputeRuntime(modelComputeRuntimeState.get());

    sceneControllerState->focusOnVisibleModel();

    NodeRuntimeServices nodeRuntimeServices{};
    nodeRuntimeServices.sceneController = sceneControllerState.get();
    nodeRuntimeServices.runtimeComputePackageController = runtimeComputePackageControllerState.get();
    nodeRuntimeServices.runtimeDisplayPackageController = runtimeDisplayPackageControllerState.get();
    nodeRuntimeServices.heatSystemController = heatSystemComputeControllerState.get();
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

bool RenderContext::initializeInputPipeline(SceneContext& scene, InputActionHandler& inputActions) {
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
        inputActions);

    RenderRuntimeServices renderRuntimeServices{
        *resourceManager,
        *meshModifiers,
        *uniformBufferManager,
        heatSystemComputeControllerState.get(),
        heatSystemDisplayControllerState.get(),
        contactSystemComputeControllerState.get(),
        voronoiSystemComputeControllerState.get(),
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
    runtimeComputePackageControllerState.reset();
    runtimeDisplayPackageControllerState.reset();
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
    contactSystemDisplayControllerState.reset();
    contactSystemComputeControllerState.reset();
    runtimeVoronoiDisplayTransportState.reset();
    runtimeVoronoiComputeTransportState.reset();
    runtimeProductRegistryState.reset();
    runtimeModelComputeTransportState.reset();
    modelDisplayRuntimeState.reset();
    modelComputeRuntimeState.reset();
    voronoiDisplayControllerState.reset();
    voronoiSystemComputeControllerState.reset();
    heatSystemComputeControllerState.reset();
    heatSystemDisplayControllerState.reset();
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

RuntimeComputePackageController* RenderContext::runtimeComputePackageController() {
    return runtimeComputePackageControllerState.get();
}

RuntimeDisplayPackageController* RenderContext::runtimeDisplayPackageController() {
    return runtimeDisplayPackageControllerState.get();
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
