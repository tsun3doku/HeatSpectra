#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>
#include <set>
#include <stdexcept>
#include <iostream>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsAndComputeFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
    }
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

class VulkanDevice {
public:
    VulkanDevice() = default;

    void init(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& deviceExtensions,
        const std::vector<const char*>& validationLayers, bool enableValidationLayers);

    void cleanup();

    // Getters
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
    VkCommandPool getCommandPool() const {
        return commandPool;
    }
    VkPhysicalDeviceProperties getPhysicalDeviceProperties() const {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        return properties;
    }

    VkResolveModeFlagBits getDepthResolveMode() const { 
        return depthResolveMode; 
    }
  
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory);

private:
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    std::vector<const char*> deviceExtensions;
    std::vector<const char*> validationLayers;

    VkResolveModeFlagBits depthResolveMode;
    
    bool enableValidationLayers = true;
  
    void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    void createLogicalDevice(VkSurfaceKHR surface);
    void createCommandPool();
};

