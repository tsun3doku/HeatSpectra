#include "RenderRuntime.hpp"

#include "scene/CameraController.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "framegraph/FrameController.hpp"
#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "framegraph/FrameGraphResources.hpp"
#include "framegraph/FrameGraphVkTypes.hpp"
#include "framegraph/FrameSync.hpp"
#include "scene/GizmoController.hpp"
#include "scene/NavigationGizmoController.hpp"
#include "scene/IBLSystem.hpp"
#include "scene/InputController.hpp"
#include "scene/LightingSystem.hpp"
#include "MainRenderGraph.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "scene/MaterialSystem.hpp"
#include "scene/ModelSelection.hpp"
#include "RenderConfig.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "SceneRenderer.hpp"
#include "app/ViewportTarget.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphBackend.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "renderers/WireframeRenderer.hpp"

#include <iostream>

RenderRuntime::RenderRuntime(
    const WindowRuntimeState& windowState,
    VulkanDevice& vulkanDevice,
    ViewportTarget& viewportTarget,
    CommandPool& renderCommandPool,
    FrameSync& frameSync,
    CameraController& cameraController,
    std::atomic<bool>& isOperating,
    std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      vulkanDevice(vulkanDevice),
      viewportTarget(viewportTarget),
      renderCommandPool(renderCommandPool),
      frameSync(frameSync),
      cameraController(cameraController),
      isOperating(isOperating),
      isShuttingDown(isShuttingDown) {
}

RenderRuntime::~RenderRuntime() = default;

bool RenderRuntime::initializeBase(VkFormat swapChainFormat, VkExtent2D extent, MemoryAllocator& allocator, ModelRegistry& modelRegistry, UniformBufferManager& ubo, IBLSystem& iblSystem) {
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
    if (!frameGraphBackend->rebuild(frameGraph->getFrameGraphResult(), viewportTarget.getImageViews(), extent, renderconfig::MaxFramesInFlight)) {
        return false;
    }

    sceneRenderer = std::make_unique<SceneRenderer>(
        vulkanDevice,
        allocator,
        *frameGraph,
        frameGraphBackend->getRuntime(),
        modelRegistry,
        ubo,
        renderconfig::MaxFramesInFlight,
        renderCommandPool,
        iblSystem);
    if (!sceneRenderer) {
        return false;
    }
    if (!sceneRenderer->isReady()) {
        return false;
    }
    sceneRenderer->updateDescriptorSets();

    modelSelection = std::make_unique<ModelSelection>(
        vulkanDevice,
        frameGraphBackend->getRuntime(),
        modelRegistry,
        frameGraph->getResourceId(framegraph::resources::PickID));
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
    navigationGizmoController = std::make_unique<NavigationGizmoController>(cameraController);
    if (!navigationGizmoController) {
        return false;
    }

    wireframeRenderer = std::make_unique<WireframeRenderer>(
        vulkanDevice,
        sceneRenderer->getGbufferDescriptorSetLayout(),
        frameGraphBackend->getRuntime().getRenderPass(),
        framegraph::toIndex(frameGraph->getPassId(framegraph::passes::Overlay)));
    if (!wireframeRenderer) {
        return false;
    }

    return true;
}

bool RenderRuntime::initializeFrameController(const FrameControllerServices& services) {
    if (!modelSelection || !gizmoController || !navigationGizmoController || !wireframeRenderer || !frameGraph || !frameGraphBackend || !sceneRenderer) {
        return false;
    }

    frameController = std::make_unique<FrameController>(
        windowState,
        vulkanDevice,
        viewportTarget,
        *frameGraph,
        *frameGraphBackend,
        *sceneRenderer,
        frameSync,
        frameStats,
        cameraController,
        services,
        isOperating,
        isShuttingDown);

    return frameController != nullptr;
}

bool RenderRuntime::renderFrame(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    const render::RenderFlags& flags,
    const std::vector<ComputePass*>& computePasses) {
    if (frameController) {
        return frameController->drawFrame(commandBuffer, frameIndex, flags, computePasses);
    }
    return false;
}

bool RenderRuntime::updateViewportTarget(VkImage image, VkFormat format, VkExtent2D extent) {
    if (!frameGraph || !frameGraphBackend || !sceneRenderer) {
        return false;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    frameGraphBackend->cleanup(renderconfig::MaxFramesInFlight);
    if (!viewportTarget.update(image, format, extent)) {
        return false;
    }
    if (!frameGraph->compile(
            framegraph::vk::toFrameGraphFormat(format),
            framegraph::vk::toFrameGraphExtent(extent))) {
        return false;
    }
    if (!frameGraphBackend->rebuild(
            frameGraph->getFrameGraphResult(),
            viewportTarget.getImageViews(),
            extent,
            renderconfig::MaxFramesInFlight)) {
        return false;
    }
    sceneRenderer->resize(extent);
    sceneRenderer->updateDescriptorSets();
    return true;
}

void RenderRuntime::cleanup() {
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
    navigationGizmoController.reset();
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

NavigationGizmoController& RenderRuntime::getNavigationGizmoController() {
    return *navigationGizmoController;
}

const NavigationGizmoController& RenderRuntime::getNavigationGizmoController() const {
    return *navigationGizmoController;
}

WireframeRenderer& RenderRuntime::getWireframeRenderer() {
    return *wireframeRenderer;
}

const WireframeRenderer& RenderRuntime::getWireframeRenderer() const {
    return *wireframeRenderer;
}

