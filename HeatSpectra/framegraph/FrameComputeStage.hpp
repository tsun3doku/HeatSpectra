#pragma once

#include <cstdint>
#include <vector>

#include "FrameTypes.hpp"

class VulkanDevice;
class FrameSync;
class ComputeTiming;
class ComputePass;
class VkFrameGraphRuntime;

class FrameComputeStage {
public:
    FrameComputeStage(VulkanDevice& vulkanDevice, VkFrameGraphRuntime& frameGraphRuntime, FrameSync& frameSync, ComputeTiming& computeTiming);
    FrameStageResult execute(uint32_t frameIndex, const std::vector<ComputePass*>& computePasses, FrameSyncState& syncState);

private:
    VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    FrameSync& frameSync;
    ComputeTiming& computeTiming;
};