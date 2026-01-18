#pragma once

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include <vulkan/vulkan.h>

// Helper functions for creating various types of Vulkan buffers

// Create a storage buffer with optional host visibility
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

// Create a texel buffer (uniform texel buffer) with buffer view
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
    VkDeviceSize alignment = 256  // Default 256 for uniform texel buffers, can override
);

// Create a staging buffer for CPU-to-GPU transfers
void createStagingBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
);

// Create a uniform buffer (for UBOs)
void createUniformBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
);

// Create a vertex buffer (device local)
void createVertexBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    VkBufferUsageFlags additionalUsage = 0
);
