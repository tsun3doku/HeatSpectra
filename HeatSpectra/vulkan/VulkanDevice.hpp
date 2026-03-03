#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <cstdint>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> graphicsAndComputeFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanDevice {
public:
    VulkanDevice() = default;

    void importExternal(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex);
    void cleanup();

    VkPhysicalDevice getPhysicalDevice() const {
        return physicalDevice;
    }

    VkDevice getDevice() const {
        return device;
    }

    VkQueue getGraphicsQueue() const {
        return graphicsQueue;
    }

    VkQueue getPresentQueue() const {
        return presentQueue;
    }

    VkQueue getComputeQueue() const {
        return computeQueue;
    }

    VkPhysicalDeviceProperties getPhysicalDeviceProperties() const {
        return physicalDeviceProperties;
    }

    VkResolveModeFlagBits getDepthResolveMode() const {
        return depthResolveMode;
    }

    QueueFamilyIndices getQueueFamilyIndices() const {
        return queueFamilyIndices;
    }

    bool findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t& outMemoryTypeIndex) const;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    VkResult createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkDeviceMemory& bufferMemory,
        VkBuffer& outBuffer);
    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory);

private:
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;

    VkResolveModeFlagBits depthResolveMode = VK_RESOLVE_MODE_NONE;
    QueueFamilyIndices queueFamilyIndices;
    VkPhysicalDeviceProperties physicalDeviceProperties{};
};
