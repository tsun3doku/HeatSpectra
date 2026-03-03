#include "RenderTargetStage.hpp"

#include <iostream>
#include <thread>

#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameSimulation.hpp"
#include "framegraph/FrameGraphVkTypes.hpp"
#include "framegraph/FrameSync.hpp"
#include "RenderConfig.hpp"
#include "SceneRenderer.hpp"
#include "RenderTargetManager.hpp"
#include "framegraph/VkFrameGraphBackend.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "WindowRuntimeState.hpp"

RenderTargetStage::RenderTargetStage(const WindowRuntimeState& windowState, VulkanDevice& vulkanDevice, RenderTargetManager& renderTargetManager,
    FrameGraph& frameGraph, VkFrameGraphBackend& frameGraphBackend, SceneRenderer& sceneRenderer, FrameSync& frameSync,
    FrameSimulation* simulation, std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      vulkanDevice(vulkanDevice),
      renderTargetManager(renderTargetManager),
      frameGraph(frameGraph),
      frameGraphBackend(frameGraphBackend),
      sceneRenderer(sceneRenderer),
      frameSync(frameSync),
      simulation(simulation),
      isShuttingDown(isShuttingDown) {
}

bool RenderTargetStage::initializeSyncObjects() {
    return frameSync.initialize(vulkanDevice.getDevice(), renderconfig::MaxFramesInFlight);
}

void RenderTargetStage::shutdownSyncObjects() {
    frameSync.shutdown();
}

void RenderTargetStage::cleanupSwapChain() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    frameGraphBackend.cleanup(renderconfig::MaxFramesInFlight);
    sceneRenderer.freeCommandBuffers();
    renderTargetManager.cleanup();
}

void RenderTargetStage::setSimulation(FrameSimulation* updatedSimulation) {
    simulation = updatedSimulation;
}

bool RenderTargetStage::recreateSwapChain() {
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
    if (!renderTargetManager.create()) {
        return false;
    }

    const VkExtent2D targetExtent = renderTargetManager.getExtent();
    const VkFormat targetFormat = renderTargetManager.getImageFormat();

    if (!frameGraph.compile(
            framegraph::vk::toFrameGraphFormat(targetFormat),
            framegraph::vk::toFrameGraphExtent(targetExtent))) {
        return false;
    }
    if (!frameGraphBackend.rebuild(
            frameGraph.getFrameGraphResult(),
            renderTargetManager.getImageViews(),
            targetExtent,
            renderconfig::MaxFramesInFlight)) {
        return false;
    }

    if (simulation) {
        simulation->recreateResources(
            renderconfig::MaxFramesInFlight,
            targetExtent,
            frameGraphBackend.getRuntime().getRenderPass());
    }

    sceneRenderer.updateDescriptorSets();
    sceneRenderer.resize(targetExtent);
    if (!sceneRenderer.createCommandBuffers()) {
        std::cerr << "[RenderTargetStage] Failed to recreate scene command buffers" << std::endl;
        return false;
    }

    if (!initializeSyncObjects()) {
        return false;
    }
    frameSync.resetFrameIndex();
    return true;
}

