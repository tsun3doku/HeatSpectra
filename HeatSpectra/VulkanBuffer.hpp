#pragma once

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include <vulkan/vulkan.h>

void createStorageBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    const void* data,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr,
    bool hostVisible = true,
    VkBufferUsageFlags additionalUsage = 0
);

void createTexelBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    const void* data,
    VkDeviceSize size,
    VkFormat format,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    VkBufferView& outBufferView,
    VkBufferUsageFlags additionalUsage = 0,
    VkDeviceSize alignment = 256  
);

void createStagingBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
);

void createUniformBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
);

void createVertexBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    VkBufferUsageFlags additionalUsage = 0
);
