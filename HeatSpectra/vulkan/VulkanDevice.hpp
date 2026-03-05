#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <cstdint>
#include <vector>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> graphicsAndComputeFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice() = default;

    void init(
        VkInstance instance,
        VkSurfaceKHR surface,
        const std::vector<const char*>& deviceExtensions,
        const std::vector<const char*>& validationLayers,
        bool enableValidationLayers);
    void importExternal(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkQueue graphicsQueue,
        uint32_t queueFamilyIndex,
        VkSurfaceKHR surface = VK_NULL_HANDLE);
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

    VkSurfaceKHR getSurface() const {
        return surface;
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

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

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
    void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void createLogicalDevice(VkSurfaceKHR surface);
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    void chooseDepthResolveMode();

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;

    VkResolveModeFlagBits depthResolveMode = VK_RESOLVE_MODE_NONE;
    QueueFamilyIndices queueFamilyIndices;
    VkPhysicalDeviceProperties physicalDeviceProperties{};

    std::vector<const char*> deviceExtensions;
    std::vector<const char*> validationLayers;
    bool enableValidationLayers = false;
    bool ownsDevice = false;
};
