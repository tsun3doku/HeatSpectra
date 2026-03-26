#include "FrameComputeStage.hpp"

#include <iostream>

#include "heat/HeatSystem.hpp"
#include "util/ComputeTiming.hpp"
#include "FrameSync.hpp"
#include "VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"

FrameComputeStage::FrameComputeStage(VulkanDevice& vulkanDevice, VkFrameGraphRuntime& frameGraphRuntime, FrameSync& frameSync, ComputeTiming& computeTiming)
    : vulkanDevice(vulkanDevice),
      frameGraphRuntime(frameGraphRuntime),
      frameSync(frameSync),
      computeTiming(computeTiming) {
}

FrameStageResult FrameComputeStage::execute(uint32_t frameIndex, HeatSystem* heatSystem, FrameSyncState& syncState, bool allowHeatSolve) {
    syncState = {};
    if (!allowHeatSolve || !heatSystem) {
        return FrameStageResult::Continue;
    }

    heatSystem->processResetRequest();
    heatSystem->update();

    const bool hasComputeWritesForGraphics = heatSystem->hasDispatchableComputeWork();
    const bool queuesAreShared = vulkanDevice.getComputeQueue() == vulkanDevice.getGraphicsQueue();
    bool computeSubmittedThisFrame = false;

    if (hasComputeWritesForGraphics) {
        const auto& computeCommandBuffers = heatSystem->getComputeCommandBuffers();
        if (frameIndex >= computeCommandBuffers.size()) {
            std::cout << "[FrameComputeStage] Missing simulation compute command buffer for frame index" << std::endl;
            syncState = {};
            return FrameStageResult::Fatal;
        }

        computeTiming.markFrameValid(frameIndex, false);
        VkCommandBuffer computeCommandBuffer = computeCommandBuffers[frameIndex];
        vkResetCommandBuffer(computeCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        frameSync.prepareComputeSubmit();
        heatSystem->recordComputeCommands(computeCommandBuffer, frameIndex, computeTiming.getQueryPool(), computeTiming.getQueryBase(frameIndex));

        const VkResult computeSubmitResult = frameSync.submitCompute(vulkanDevice.getComputeQueue(), computeCommandBuffer, !queuesAreShared);

        if (computeSubmitResult != VK_SUCCESS) {
            std::cout << "[FrameComputeStage] compute vkQueueSubmit FAILED with result=" << computeSubmitResult;
            if (computeSubmitResult == VK_ERROR_DEVICE_LOST) {
                std::cout << " (VK_ERROR_DEVICE_LOST). Treating as fatal." << std::endl;
                syncState = {};
                return FrameStageResult::Fatal;
            }

            std::cout << " Triggering swapchain recreation" << std::endl;
            syncState = {};
            return FrameStageResult::RecreateSwapchain;
        }

        computeSubmittedThisFrame = true;
        computeTiming.markFrameValid(frameIndex, true);
    }

    if (hasComputeWritesForGraphics) {
        syncState.waitDstStageMask = frameGraphRuntime.getComputeToGraphicsWaitDstStageMask();
        syncState.insertComputeToGraphicsBarrier = queuesAreShared && computeSubmittedThisFrame;
        syncState.waitForComputeSemaphore = !queuesAreShared && computeSubmittedThisFrame;
        if (syncState.waitForComputeSemaphore && syncState.waitDstStageMask == 0) {
            syncState.waitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
    }

    return FrameStageResult::Continue;
}
