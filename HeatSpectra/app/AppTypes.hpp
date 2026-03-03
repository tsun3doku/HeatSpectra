#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct AppVulkanContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

struct AppViewportOutput {
    uint64_t imageHandle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    uint64_t generation = 0;
    bool valid = false;
};
