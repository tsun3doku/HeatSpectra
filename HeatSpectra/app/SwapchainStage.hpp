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
class HeatSystemComputeController;
class ContactSystemComputeController;
class VoronoiSystemComputeController;
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
        HeatSystemComputeController* heatSystemController,
        ContactSystemComputeController* contactSystemController,
        VoronoiSystemComputeController* voronoiSystemController,
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
    HeatSystemComputeController* heatSystemController = nullptr;
    ContactSystemComputeController* contactSystemController = nullptr;
    VoronoiSystemComputeController* voronoiSystemController = nullptr;
    std::atomic<bool>& isShuttingDown;
};
