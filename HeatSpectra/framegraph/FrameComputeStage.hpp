#pragma once

#include <cstdint>

#include "FrameTypes.hpp"

class VulkanDevice;
class FrameSync;
class ComputeTiming;
class HeatSystem;
class VkFrameGraphRuntime;

class FrameComputeStage {
public:
    FrameComputeStage(VulkanDevice& vulkanDevice, VkFrameGraphRuntime& frameGraphRuntime, FrameSync& frameSync, ComputeTiming& computeTiming);
    FrameStageResult execute(uint32_t frameIndex, HeatSystem* heatSystem, FrameSyncState& syncState, bool allowHeatSolve);

private:
    VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    FrameSync& frameSync;
    ComputeTiming& computeTiming;
};
