#include "HeatPlaybackRuntime.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "util/Structs.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <cstring>
#include <iostream>

bool HeatPlaybackRuntime::initialize(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator) {
    if (isInitialized()) {
        reset();
        return true;
    }

    cleanup(memoryAllocator);

    if (createUniformBuffer(memoryAllocator, vulkanDevice, sizeof(heat::SimPlaybackUniform), playbackBuffer, playbackBufferOffset, &mappedPlaybackData) != VK_SUCCESS) {
        cleanup(memoryAllocator);
        return false;
    }

    reset();
    return true;
}

void HeatPlaybackRuntime::reset() {
    if (mappedPlaybackData) {
        std::memset(mappedPlaybackData, 0, sizeof(heat::SimPlaybackUniform));
    }
}

void HeatPlaybackRuntime::cleanup(MemoryAllocator& memoryAllocator) {
    freeBuffer(memoryAllocator, playbackBuffer, playbackBufferOffset);
    mappedPlaybackData = nullptr;
}

heat::SimPlaybackUniform* HeatPlaybackRuntime::getMappedPlaybackData() const {
    return static_cast<heat::SimPlaybackUniform*>(mappedPlaybackData);
}
