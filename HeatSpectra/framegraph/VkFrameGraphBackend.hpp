#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "FrameGraphTypes.hpp"
#include "VkFrameGraphRuntime.hpp"

class VulkanDevice;
class MemoryAllocator;

class VkFrameGraphBackend {
public:
    VkFrameGraphBackend(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);

    bool rebuild(const framegraph::FrameGraphResult& result, const std::vector<VkImageView>& swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight);
    void cleanup(uint32_t maxFramesInFlight);

    VkFrameGraphRuntime& getRuntime() { return runtime; }
    const VkFrameGraphRuntime& getRuntime() const { return runtime; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    VkFrameGraphRuntime runtime;
};
