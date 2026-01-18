#include "VulkanBuffer.hpp"
#include <stdexcept>
#include <cstring>
#include <iostream>

void createStorageBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    const void* data,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr,
    bool hostVisible,
    VkBufferUsageFlags additionalUsage
) {
    if (size == 0) {
        throw std::runtime_error("[createStorageBuffer] Buffer size cannot be zero");
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalUsage;
    VkMemoryPropertyFlags properties = hostVisible 
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    
    VkDeviceSize alignment = device.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;

    if (hostVisible) {
        void* mappedPtr = allocator.getMappedPointer(buffer, offset);
        if (!mappedPtr) {
            throw std::runtime_error("[createStorageBuffer] Failed to map buffer memory");
        }
        
        // Copy data if provided
        if (data) {
            memcpy(mappedPtr, data, size);
        }
        
        if (outMappedPtr) {
            *outMappedPtr = mappedPtr;
        }
    } else {
        if (outMappedPtr) {
            *outMappedPtr = nullptr;
        }
    }
}

void createTexelBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    const void* data,
    VkDeviceSize size,
    VkFormat format,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    VkBufferView& outBufferView,
    VkBufferUsageFlags additionalUsage,
    VkDeviceSize alignment
) {
    if (size == 0) {
        throw std::runtime_error("[createTexelBuffer] Buffer size cannot be zero");
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | additionalUsage;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;

    // Upload data if provided
    if (data) {
        void* mapped = allocator.getMappedPointer(buffer, offset);
        if (!mapped) {
            throw std::runtime_error("[createTexelBuffer] Failed to map buffer for upload");
        }
        memcpy(mapped, data, size);
    }

    // Create buffer view
    VkBufferViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.buffer = buffer;
    viewInfo.format = format;
    viewInfo.offset = offset;
    viewInfo.range = size;

    if (vkCreateBufferView(device.getDevice(), &viewInfo, nullptr, &outBufferView) != VK_SUCCESS) {
        throw std::runtime_error("[createTexelBuffer] Failed to create buffer view");
    }
}

void createStagingBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
) {
    if (size == 0) {
        throw std::runtime_error("[createStagingBuffer] Buffer size cannot be zero");
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto [buffer, offset] = allocator.allocate(size, usage, properties);
    outBuffer = buffer;
    outOffset = offset;

    void* mappedPtr = allocator.getMappedPointer(buffer, offset);
    if (!mappedPtr) {
        throw std::runtime_error("[createStagingBuffer] Failed to map staging buffer");
    }

    if (outMappedPtr) {
        *outMappedPtr = mappedPtr;
    }
}

void createUniformBuffer(
    MemoryAllocator& allocator,
    VulkanDevice& device,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    void** outMappedPtr
) {
    if (size == 0) {
        throw std::runtime_error("[createUniformBuffer] Buffer size cannot be zero");
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    
    VkDeviceSize alignment = device.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;

    void* mappedPtr = allocator.getMappedPointer(buffer, offset);
    if (!mappedPtr) {
        throw std::runtime_error("[createUniformBuffer] Failed to map uniform buffer");
    }

    if (outMappedPtr) {
        *outMappedPtr = mappedPtr;
    }
}

void createVertexBuffer(
    MemoryAllocator& allocator,
    VkDeviceSize size,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset,
    VkBufferUsageFlags additionalUsage
) {
    if (size == 0) {
        throw std::runtime_error("[createVertexBuffer] Buffer size cannot be zero");
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsage;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    
    // 16-byte alignment is typical for vertex buffers
    VkDeviceSize alignment = 16;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;
}
