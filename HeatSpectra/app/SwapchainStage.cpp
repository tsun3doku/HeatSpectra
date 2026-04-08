#include "SwapchainStage.hpp"

#include "SwapchainManager.hpp"
#include "contact/ContactSystemController.hpp"
#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameGraphVkTypes.hpp"
#include "framegraph/FrameSync.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemController.hpp"
#include "heat/VoronoiSystem.hpp"
#include "heat/VoronoiSystemController.hpp"
#include "framegraph/VkFrameGraphBackend.hpp"
#include "render/RenderConfig.hpp"
#include "render/SceneRenderer.hpp"
#include "render/WindowRuntimeState.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>

SwapchainStage::SwapchainStage(
    const WindowRuntimeState& windowState,
    VulkanDevice& vulkanDevice,
    SwapchainManager& swapchainManager,
    FrameGraph& frameGraph,
    VkFrameGraphBackend& frameGraphBackend,
    SceneRenderer& sceneRenderer,
    FrameSync& frameSync,
    HeatSystemController* heatSystemController,
    ContactSystemController* contactSystemController,
    VoronoiSystemController* voronoiSystemController,
    std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      vulkanDevice(vulkanDevice),
      swapchainManager(swapchainManager),
      frameGraph(frameGraph),
      frameGraphBackend(frameGraphBackend),
      sceneRenderer(sceneRenderer),
      frameSync(frameSync),
      heatSystemController(heatSystemController),
      contactSystemController(contactSystemController),
      voronoiSystemController(voronoiSystemController),
      isShuttingDown(isShuttingDown) {
}

bool SwapchainStage::initializeSyncObjects() {
    return frameSync.initialize(vulkanDevice.getDevice(), renderconfig::MaxFramesInFlight);
}

void SwapchainStage::shutdownSyncObjects() {
    frameSync.shutdown();
}

void SwapchainStage::cleanupSwapChain() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    frameGraphBackend.cleanup(renderconfig::MaxFramesInFlight);
    sceneRenderer.freeCommandBuffers();
    swapchainManager.cleanup();
}

bool SwapchainStage::recreateSwapChain() {
    if (isShuttingDown.load(std::memory_order_acquire)) {
        return false;
    }

    uint32_t width = windowState.width.load(std::memory_order_acquire);
    uint32_t height = windowState.height.load(std::memory_order_acquire);
    while (width < static_cast<uint32_t>(renderconfig::MinSwapchainExtent) ||
           height < static_cast<uint32_t>(renderconfig::MinSwapchainExtent)) {
        if (windowState.shouldClose.load(std::memory_order_acquire)) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::RenderPauseSleepMs));
        width = windowState.width.load(std::memory_order_acquire);
        height = windowState.height.load(std::memory_order_acquire);
    }

    cleanupSwapChain();
    frameSync.shutdown();
    if (!swapchainManager.create()) {
        return false;
    }

    const VkExtent2D targetExtent = swapchainManager.getExtent();
    const VkFormat targetFormat = swapchainManager.getImageFormat();

    if (!frameGraph.compile(
            framegraph::vk::toFrameGraphFormat(targetFormat),
            framegraph::vk::toFrameGraphExtent(targetExtent))) {
        return false;
    }

    if (!frameGraphBackend.rebuild(
            frameGraph.getFrameGraphResult(),
            swapchainManager.getImageViews(),
            targetExtent,
            renderconfig::MaxFramesInFlight)) {
        return false;
    }

    if (heatSystemController) {
        heatSystemController->updateRenderContext(
            targetExtent,
            frameGraphBackend.getRuntime().getRenderPass());
        heatSystemController->updateRenderResources();
    }
    if (contactSystemController) {
        contactSystemController->updateRenderContext(
            targetExtent,
            frameGraphBackend.getRuntime().getRenderPass());
        contactSystemController->updateRenderResources();
    }
    if (voronoiSystemController) {
        voronoiSystemController->updateRenderContext(
            targetExtent,
            frameGraphBackend.getRuntime().getRenderPass());
        voronoiSystemController->updateRenderResources();
    }

    sceneRenderer.updateDescriptorSets();
    sceneRenderer.resize(targetExtent);
    if (!sceneRenderer.createCommandBuffers()) {
        std::cerr << "[SwapchainStage] Failed to recreate scene command buffers" << std::endl;
        return false;
    }

    if (!initializeSyncObjects()) {
        return false;
    }
    frameSync.resetFrameIndex();
    return true;
}

FrameStageResult SwapchainStage::acquireFrameImage(uint32_t& imageIndex) {
    const VkResult result = frameSync.acquireNextImage(swapchainManager.getSwapChain(), imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return FrameStageResult::RecreateSwapchain;
    }

    if (result != VK_SUCCESS) {
        return FrameStageResult::Fatal;
    }

    return FrameStageResult::Continue;
}

FrameStageResult SwapchainStage::presentFrame(uint32_t imageIndex) {
    const VkResult result = frameSync.present(vulkanDevice.getPresentQueue(), swapchainManager.getSwapChain(), imageIndex);
    if (result == VK_SUCCESS) {
        return FrameStageResult::Continue;
    }
    return FrameStageResult::RecreateSwapchain;
}
