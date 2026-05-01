#include "HeatSystemSimRuntime.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "util/Structs.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <cstring>

static void freeRuntimeBuffer(MemoryAllocator& memoryAllocator, VkBuffer& buffer, VkDeviceSize& offset) {
    if (buffer != VK_NULL_HANDLE) {
        memoryAllocator.free(buffer, offset);
        buffer = VK_NULL_HANDLE;
        offset = 0;
    }
}

bool HeatSystemSimRuntime::initialize(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t requestedNodeCount) {
    if (matchesNodeCount(requestedNodeCount)) {
        reset();
        return true;
    }

    cleanup(memoryAllocator);
    nodeCount = requestedNodeCount;

    void* mappedPtr = nullptr;
    const VkDeviceSize tempBufferSize = sizeof(float) * static_cast<VkDeviceSize>(nodeCount);
    if (createStorageBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            tempBufferSize,
            tempBufferA,
            tempBufferAOffset,
            &mappedPtr) != VK_SUCCESS ||
        tempBufferA == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        cleanup(memoryAllocator);
        return false;
    }
    mappedTempBufferA = mappedPtr;

    if (createStorageBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            tempBufferSize,
            tempBufferB,
            tempBufferBOffset,
            &mappedPtr) != VK_SUCCESS ||
        tempBufferB == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        cleanup(memoryAllocator);
        return false;
    }
    mappedTempBufferB = mappedPtr;

    const VkDeviceSize timeBufferSize = sizeof(heat::TimeUniform);
    if (createUniformBuffer(
            memoryAllocator,
            vulkanDevice,
            timeBufferSize,
            timeBuffer,
            timeBufferOffset,
            &mappedPtr) != VK_SUCCESS ||
        timeBuffer == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        cleanup(memoryAllocator);
        return false;
    }
    mappedTimeData = mappedPtr;

    reset();
    return true;
}

void HeatSystemSimRuntime::reset() {
    float* tempsA = static_cast<float*>(mappedTempBufferA);
    float* tempsB = static_cast<float*>(mappedTempBufferB);
    for (uint32_t index = 0; index < nodeCount; ++index) {
        tempsA[index] = AMBIENT_TEMPERATURE;
        tempsB[index] = AMBIENT_TEMPERATURE;
    }

    if (mappedTimeData) {
        auto* timeData = static_cast<heat::TimeUniform*>(mappedTimeData);
        timeData->deltaTime = 0.0f;
        timeData->totalTime = 0.0f;
    }
}

void HeatSystemSimRuntime::cleanup(MemoryAllocator& memoryAllocator) {
    freeRuntimeBuffer(memoryAllocator, timeBuffer, timeBufferOffset);
    freeRuntimeBuffer(memoryAllocator, tempBufferA, tempBufferAOffset);
    freeRuntimeBuffer(memoryAllocator, tempBufferB, tempBufferBOffset);

    mappedTimeData = nullptr;
    mappedTempBufferA = nullptr;
    mappedTempBufferB = nullptr;
    nodeCount = 0;
}

heat::TimeUniform* HeatSystemSimRuntime::getMappedTimeData() const {
    return static_cast<heat::TimeUniform*>(mappedTimeData);
}
