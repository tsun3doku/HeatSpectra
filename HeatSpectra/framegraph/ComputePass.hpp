#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class ComputePass {
public:
    virtual ~ComputePass() = default;

    virtual void update() = 0;
    virtual bool hasDispatchableComputeWork() const = 0;
    virtual const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const = 0;
    virtual void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame,
        VkQueryPool timingQueryPool, uint32_t timingQueryBase) = 0;
};