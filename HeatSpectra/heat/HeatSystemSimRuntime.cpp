#include "HeatSystemSimRuntime.hpp"

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

    const VkDeviceSize injectionBufferSize = sizeof(uint32_t) * static_cast<VkDeviceSize>(nodeCount);
    if (createStorageBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            injectionBufferSize,
            injectionKBuffer,
            injectionKBufferOffset,
            &mappedPtr) != VK_SUCCESS ||
        injectionKBuffer == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        cleanup(memoryAllocator);
        return false;
    }
    mappedInjectionKBuffer = mappedPtr;

    if (createStorageBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            injectionBufferSize,
            injectionKTBuffer,
            injectionKTBufferOffset,
            &mappedPtr) != VK_SUCCESS ||
        injectionKTBuffer == VK_NULL_HANDLE ||
        mappedPtr == nullptr) {
        cleanup(memoryAllocator);
        return false;
    }
    mappedInjectionKTBuffer = mappedPtr;

    const VkDeviceSize timeBufferSize = sizeof(TimeUniform);
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

    if (mappedInjectionKBuffer) {
        std::memset(mappedInjectionKBuffer, 0, sizeof(uint32_t) * static_cast<size_t>(nodeCount));
    }
    if (mappedInjectionKTBuffer) {
        std::memset(mappedInjectionKTBuffer, 0, sizeof(uint32_t) * static_cast<size_t>(nodeCount));
    }
    if (mappedTimeData) {
        auto* timeData = static_cast<TimeUniform*>(mappedTimeData);
        timeData->deltaTime = 0.0f;
        timeData->totalTime = 0.0f;
    }

}

void HeatSystemSimRuntime::cleanup(MemoryAllocator& memoryAllocator) {
    freeRuntimeBuffer(memoryAllocator, timeBuffer, timeBufferOffset);
    freeRuntimeBuffer(memoryAllocator, tempBufferA, tempBufferAOffset);
    freeRuntimeBuffer(memoryAllocator, tempBufferB, tempBufferBOffset);
    freeRuntimeBuffer(memoryAllocator, injectionKBuffer, injectionKBufferOffset);
    freeRuntimeBuffer(memoryAllocator, injectionKTBuffer, injectionKTBufferOffset);

    mappedTimeData = nullptr;
    mappedTempBufferA = nullptr;
    mappedTempBufferB = nullptr;
    mappedInjectionKBuffer = nullptr;
    mappedInjectionKTBuffer = nullptr;
    nodeCount = 0;
}

TimeUniform* HeatSystemSimRuntime::getMappedTimeData() const {
    return static_cast<TimeUniform*>(mappedTimeData);
}
