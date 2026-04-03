#include "RenderRuntime.hpp"

#include "scene/CameraController.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "framegraph/FrameController.hpp"
#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameGraphResources.hpp"
#include "framegraph/FrameGraphVkTypes.hpp"
#include "framegraph/FrameSync.hpp"
#include "scene/GizmoController.hpp"
#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "MainRenderGraph.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "scene/MaterialSystem.hpp"
#include "mesh/MeshModifiers.hpp"
#include "scene/ModelSelection.hpp"
#include "RenderConfig.hpp"
#include "vulkan/ResourceManager.hpp"
#include "SceneRenderer.hpp"
#include "app/SwapchainManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphBackend.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "renderers/WireframeRenderer.hpp"

RenderRuntime::RenderRuntime(
    const WindowRuntimeState& windowState,
    VulkanDevice& vulkanDevice,
    SwapchainManager& swapchainManager,
    CommandPool& renderCommandPool,
    FrameSync& frameSync,
    CameraController& cameraController,
    std::atomic<bool>& isOperating,
    std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      vulkanDevice(vulkanDevice),
      swapchainManager(swapchainManager),
      renderCommandPool(renderCommandPool),
      frameSync(frameSync),
      cameraController(cameraController),
      isOperating(isOperating),
      isShuttingDown(isShuttingDown) {
}

RenderRuntime::~RenderRuntime() = default;

bool RenderRuntime::initializeBase(VkFormat swapChainFormat, VkExtent2D extent, MemoryAllocator& allocator, ResourceManager& resourceManager, UniformBufferManager& ubo) {
    if (swapChainFormat == VK_FORMAT_UNDEFINED || extent.width == 0 || extent.height == 0) {
        return false;
    }

    frameGraph = std::make_unique<FrameGraph>();
    if (!frameGraph) {
        return false;
    }
    MainRenderGraph::buildMainRenderGraph(*frameGraph);

    if (!frameGraph->compile(framegraph::vk::toFrameGraphFormat(swapChainFormat), framegraph::vk::toFrameGraphExtent(extent))) {
        return false;
    }

    frameGraphBackend = std::make_unique<VkFrameGraphBackend>(vulkanDevice, allocator);
    if (!frameGraphBackend) {
        return false;
    }
    if (!frameGraphBackend->rebuild(frameGraph->getFrameGraphResult(), swapchainManager.getImageViews(), extent, renderconfig::MaxFramesInFlight)) {
        return false;
    }

    sceneRenderer = std::make_unique<SceneRenderer>(
        vulkanDevice,
        allocator,
        *frameGraph,
        frameGraphBackend->getRuntime(),
        resourceManager,
        ubo,
        renderconfig::MaxFramesInFlight,
        renderCommandPool);
    if (!sceneRenderer) {
        return false;
    }
    if (!sceneRenderer->isReady()) {
        return false;
    }

    modelSelection = std::make_unique<ModelSelection>(
        vulkanDevice,
        frameGraphBackend->getRuntime(),
        resourceManager,
        frameGraph->getResourceId(framegraph::resources::DepthResolve));
    if (!modelSelection) {
        return false;
    }
    if (!modelSelection->isInitialized()) {
        return false;
    }
    gizmoController = std::make_unique<GizmoController>();
    if (!gizmoController) {
        return false;
    }

    computeTiming.initialize(vulkanDevice, renderconfig::MaxFramesInFlight);

    wireframeRenderer = std::make_unique<WireframeRenderer>(
        vulkanDevice,
        sceneRenderer->getGbufferDescriptorSetLayout(),
        frameGraphBackend->getRuntime().getRenderPass(),
        2);
    if (!wireframeRenderer) {
        return false;
    }

    return true;
}

bool RenderRuntime::initializeFrameController(const RenderRuntimeServices& services) {
    if (!modelSelection || !gizmoController || !wireframeRenderer || !frameGraph || !frameGraphBackend || !sceneRenderer) {
        return false;
    }

    FrameControllerServices frameServices{
        services.resourceManager,
        services.meshModifiers,
        services.uniformBufferManager,
        services.heatSystem,
        services.voronoiSystem,
        *modelSelection,
        *gizmoController,
        *wireframeRenderer,
        services.inputController,
        services.lightingSystem,
        services.materialSystem
    };

    frameController = std::make_unique<FrameController>(
        windowState,
        vulkanDevice,
        swapchainManager,
        *frameGraph,
        *frameGraphBackend,
        *sceneRenderer,
        frameSync,
        computeTiming,
        frameStats,
        cameraController,
        frameServices,
        isOperating,
        isShuttingDown);

    return frameController != nullptr;
}

bool RenderRuntime::initializeSyncObjects() {
    if (frameController) {
        return frameController->initializeSyncObjects();
    }

    return frameSync.initialize(vulkanDevice.getDevice(), renderconfig::MaxFramesInFlight);
}

void RenderRuntime::shutdownSyncObjects() {
    if (frameController) {
        frameController->shutdownSyncObjects();
        return;
    }

    frameSync.shutdown();
}

void RenderRuntime::renderFrame(const render::RenderFlags& flags, const render::OverlayParams& overlay, bool allowHeatSolve) {
    if (frameController) {
        frameController->drawFrame(flags, overlay, allowHeatSolve);
    }
}

void RenderRuntime::cleanupSwapChain() {
    if (frameController) {
        frameController->cleanupSwapChain();
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    if (frameGraphBackend) {
        frameGraphBackend->cleanup(renderconfig::MaxFramesInFlight);
    }
    if (sceneRenderer) {
        sceneRenderer->freeCommandBuffers();
    }
    swapchainManager.cleanup();
}

void RenderRuntime::cleanup() {
    computeTiming.shutdown();

    if (modelSelection) {
        modelSelection->cleanup();
    }
    if (frameGraphBackend) {
        frameGraphBackend->cleanup(renderconfig::MaxFramesInFlight);
    }
    if (sceneRenderer) {
        sceneRenderer->cleanup();
    }
    if (wireframeRenderer) {
        wireframeRenderer->cleanup();
    }

    frameController.reset();
    wireframeRenderer.reset();
    gizmoController.reset();
    modelSelection.reset();
    sceneRenderer.reset();
    frameGraphBackend.reset();
    frameGraph.reset();
}

FrameGraph& RenderRuntime::getFrameGraph() {
    return *frameGraph;
}

const FrameGraph& RenderRuntime::getFrameGraph() const {
    return *frameGraph;
}

VkFrameGraphRuntime& RenderRuntime::getFrameGraphRuntime() {
    return frameGraphBackend->getRuntime();
}

const VkFrameGraphRuntime& RenderRuntime::getFrameGraphRuntime() const {
    return frameGraphBackend->getRuntime();
}

SceneRenderer& RenderRuntime::getSceneRenderer() {
    return *sceneRenderer;
}

const SceneRenderer& RenderRuntime::getSceneRenderer() const {
    return *sceneRenderer;
}

ModelSelection& RenderRuntime::getModelSelection() {
    return *modelSelection;
}

const ModelSelection& RenderRuntime::getModelSelection() const {
    return *modelSelection;
}

GizmoController& RenderRuntime::getGizmoController() {
    return *gizmoController;
}

const GizmoController& RenderRuntime::getGizmoController() const {
    return *gizmoController;
}

