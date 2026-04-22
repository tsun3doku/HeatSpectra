#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>

#include "framegraph/FrameTypes.hpp"

struct WindowRuntimeState;
class VulkanDevice;
class SwapchainManager;
class FrameGraph;
class SceneRenderer;
class FrameSync;
class VkFrameGraphBackend;

class SwapchainStage {
public:
    SwapchainStage(
        const WindowRuntimeState& windowState,
        VulkanDevice& vulkanDevice,
        SwapchainManager& swapchainManager,
        FrameGraph& frameGraph,
        VkFrameGraphBackend& frameGraphBackend,
        SceneRenderer& sceneRenderer,
        FrameSync& frameSync,
        std::atomic<bool>& isShuttingDown);

    bool initializeSyncObjects();
    void shutdownSyncObjects();
    void cleanupSwapChain();
    bool recreateSwapChain();

    FrameStageResult acquireFrameImage(uint32_t& imageIndex);
    FrameStageResult presentFrame(uint32_t imageIndex);

private:
    const WindowRuntimeState& windowState;
    VulkanDevice& vulkanDevice;
    SwapchainManager& swapchainManager;
    FrameGraph& frameGraph;
    VkFrameGraphBackend& frameGraphBackend;
    SceneRenderer& sceneRenderer;
    FrameSync& frameSync;
    std::atomic<bool>& isShuttingDown;
};
