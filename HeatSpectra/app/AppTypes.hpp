#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct AppVulkanContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
};
