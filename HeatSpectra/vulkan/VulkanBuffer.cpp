#include "VulkanBuffer.hpp"
#include <cstring>
#include <iostream>

VkResult createStorageBuffer(MemoryAllocator& allocator, VulkanDevice& device, const void* data, VkDeviceSize size, VkBuffer& outBuffer, VkDeviceSize& outOffset,
    void** outMappedPtr, bool hostVisible, VkBufferUsageFlags additionalUsage) {
    outBuffer = VK_NULL_HANDLE;
    outOffset = 0;
    if (outMappedPtr) {
        *outMappedPtr = nullptr;
    }

    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
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
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (data) {
            memcpy(mappedPtr, data, size);
        }

        if (outMappedPtr) {
            *outMappedPtr = mappedPtr;
        }
    }

    return VK_SUCCESS;
}

VkResult createTexelBuffer(MemoryAllocator& allocator, VulkanDevice& device, const void* data, VkDeviceSize size, VkFormat format, VkBuffer& outBuffer,
    VkDeviceSize& outOffset, VkBufferView& outBufferView, VkBufferUsageFlags additionalUsage, VkDeviceSize alignment) {
    outBuffer = VK_NULL_HANDLE;
    outOffset = 0;
    outBufferView = VK_NULL_HANDLE;

    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | additionalUsage;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;

    if (data) {
        void* mapped = allocator.getMappedPointer(buffer, offset);
        if (!mapped) {
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        memcpy(mapped, data, size);
    }

    VkBufferViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.buffer = buffer;
    viewInfo.format = format;
    viewInfo.offset = offset;
    viewInfo.range = size;

    const VkResult viewResult = vkCreateBufferView(device.getDevice(), &viewInfo, nullptr, &outBufferView);
    if (viewResult != VK_SUCCESS) {
        return viewResult;
    }

    return VK_SUCCESS;
}

VkResult createStagingBuffer(MemoryAllocator& allocator, VkDeviceSize size, VkBuffer& outBuffer, VkDeviceSize& outOffset, void** outMappedPtr) {
    outBuffer = VK_NULL_HANDLE;
    outOffset = 0;
    if (outMappedPtr) {
        *outMappedPtr = nullptr;
    }

    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto [buffer, offset] = allocator.allocate(size, usage, properties);
    outBuffer = buffer;
    outOffset = offset;

    void* mappedPtr = allocator.getMappedPointer(buffer, offset);
    if (!mappedPtr) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (outMappedPtr) {
        *outMappedPtr = mappedPtr;
    }

    return VK_SUCCESS;
}

VkResult createUniformBuffer(MemoryAllocator& allocator, VulkanDevice& device, VkDeviceSize size, VkBuffer& outBuffer, VkDeviceSize& outOffset, void** outMappedPtr) {
    outBuffer = VK_NULL_HANDLE;
    outOffset = 0;
    if (outMappedPtr) {
        *outMappedPtr = nullptr;
    }

    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkDeviceSize alignment = device.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;

    void* mappedPtr = allocator.getMappedPointer(buffer, offset);
    if (!mappedPtr) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (outMappedPtr) {
        *outMappedPtr = mappedPtr;
    }

    return VK_SUCCESS;
}

VkResult createVertexBuffer(MemoryAllocator& allocator, VkDeviceSize size, VkBuffer& outBuffer, VkDeviceSize& outOffset, VkBufferUsageFlags additionalUsage) {
    outBuffer = VK_NULL_HANDLE;
    outOffset = 0;

    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsage;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkDeviceSize alignment = 16;

    auto [buffer, offset] = allocator.allocate(size, usage, properties, alignment);
    outBuffer = buffer;
    outOffset = offset;
    return VK_SUCCESS;
}
