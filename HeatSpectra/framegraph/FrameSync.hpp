#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class FrameSync {
public:
    bool initialize(VkDevice device, uint32_t maxFramesInFlight);
    void shutdown();

    uint32_t beginFrame() const;
    void waitForAllFrameFences() const;
    VkResult acquireNextImage(VkSwapchainKHR swapChain, uint32_t& imageIndex) const;
    void prepareGraphicsSubmit() const;
    void prepareComputeSubmit() const;

    VkResult submitCompute(VkQueue computeQueue, VkCommandBuffer commandBuffer, bool signalComputeFinished) const;
    VkResult submitGraphics(VkQueue graphicsQueue, VkCommandBuffer commandBuffer, bool waitForCompute, VkPipelineStageFlags computeWaitStageMask) const;
    VkResult present(VkQueue presentQueue, VkSwapchainKHR swapChain, uint32_t imageIndex) const;

    void advanceFrame();
    void resetFrameIndex();
    uint32_t getCurrentFrameIndex() const { return currentFrame; }

private:
    void waitForCurrentFrameFence() const;

    VkDevice device = VK_NULL_HANDLE;
    uint32_t frameCount = 0;
    uint32_t currentFrame = 0;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkSemaphore> computeFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> computeInFlightFences;
};
