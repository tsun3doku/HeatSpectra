#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

class MemoryAllocator;
class VulkanDevice;

namespace heat {
struct SimPlaybackUniform;
}

class HeatPlaybackRuntime {
public:
    bool initialize(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);
    void reset();
    void cleanup(MemoryAllocator& memoryAllocator);

    bool isInitialized() const { return playbackBuffer != VK_NULL_HANDLE; }

    VkBuffer getPlaybackBuffer() const { return playbackBuffer; }
    VkDeviceSize getPlaybackBufferOffset() const { return playbackBufferOffset; }
    heat::SimPlaybackUniform* getMappedPlaybackData() const;

private:
    VkBuffer playbackBuffer = VK_NULL_HANDLE;
    VkDeviceSize playbackBufferOffset = 0;
    void* mappedPlaybackData = nullptr;
};
