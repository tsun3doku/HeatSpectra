#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

class MemoryAllocator;
class VulkanDevice;

struct TimeUniform;

class HeatSystemSimRuntime {
public:
    bool initialize(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t nodeCount);
    void reset();
    void cleanup(MemoryAllocator& memoryAllocator);

    bool isInitialized() const { return tempBufferA != VK_NULL_HANDLE && tempBufferB != VK_NULL_HANDLE && timeBuffer != VK_NULL_HANDLE; }
    bool matchesNodeCount(uint32_t count) const { return nodeCount == count && isInitialized(); }

    uint32_t getNodeCount() const { return nodeCount; }

    VkBuffer getTempBufferA() const { return tempBufferA; }
    VkDeviceSize getTempBufferAOffset() const { return tempBufferAOffset; }
    void* getMappedTempBufferA() const { return mappedTempBufferA; }

    VkBuffer getTempBufferB() const { return tempBufferB; }
    VkDeviceSize getTempBufferBOffset() const { return tempBufferBOffset; }
    void* getMappedTempBufferB() const { return mappedTempBufferB; }

    VkBuffer getInjectionKBuffer() const { return injectionKBuffer; }
    VkDeviceSize getInjectionKBufferOffset() const { return injectionKBufferOffset; }
    void* getMappedInjectionKBuffer() const { return mappedInjectionKBuffer; }

    VkBuffer getInjectionKTBuffer() const { return injectionKTBuffer; }
    VkDeviceSize getInjectionKTBufferOffset() const { return injectionKTBufferOffset; }
    void* getMappedInjectionKTBuffer() const { return mappedInjectionKTBuffer; }

    VkBuffer getTimeBuffer() const { return timeBuffer; }
    VkDeviceSize getTimeBufferOffset() const { return timeBufferOffset; }
    TimeUniform* getMappedTimeData() const;

private:
    static constexpr float AMBIENT_TEMPERATURE = 1.0f;

    uint32_t nodeCount = 0;

    VkBuffer tempBufferA = VK_NULL_HANDLE;
    VkDeviceSize tempBufferAOffset = 0;
    void* mappedTempBufferA = nullptr;

    VkBuffer tempBufferB = VK_NULL_HANDLE;
    VkDeviceSize tempBufferBOffset = 0;
    void* mappedTempBufferB = nullptr;

    VkBuffer injectionKBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKBufferOffset = 0;
    void* mappedInjectionKBuffer = nullptr;

    VkBuffer injectionKTBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKTBufferOffset = 0;
    void* mappedInjectionKTBuffer = nullptr;

    VkBuffer timeBuffer = VK_NULL_HANDLE;
    VkDeviceSize timeBufferOffset = 0;
    void* mappedTimeData = nullptr;
};
