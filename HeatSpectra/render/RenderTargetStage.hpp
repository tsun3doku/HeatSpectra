#pragma once

#include "framegraph/FrameTypes.hpp"

#include <atomic>

struct WindowRuntimeState;
class VulkanDevice;
class RenderTargetManager;
class FrameGraph;
class SceneRenderer;
class FrameSync;
class FrameSimulation;
class VkFrameGraphBackend;

class RenderTargetStage {
public:
    RenderTargetStage(
        const WindowRuntimeState& windowState,
        VulkanDevice& vulkanDevice,
        RenderTargetManager& renderTargetManager,
        FrameGraph& frameGraph,
        VkFrameGraphBackend& frameGraphBackend,
        SceneRenderer& sceneRenderer,
        FrameSync& frameSync,
        FrameSimulation* simulation,
        std::atomic<bool>& isShuttingDown);

    bool initializeSyncObjects();
    void shutdownSyncObjects();
    void cleanupSwapChain();
    bool recreateSwapChain();
    void setSimulation(FrameSimulation* simulation);

private:
    const WindowRuntimeState& windowState;
    VulkanDevice& vulkanDevice;
    RenderTargetManager& renderTargetManager;
    FrameGraph& frameGraph;
    VkFrameGraphBackend& frameGraphBackend;
    SceneRenderer& sceneRenderer;
    FrameSync& frameSync;
    FrameSimulation* simulation = nullptr;
    std::atomic<bool>& isShuttingDown;
};
