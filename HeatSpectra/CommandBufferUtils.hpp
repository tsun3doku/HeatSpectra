#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanDevice.hpp"  // Include the VulkanDevice header if it's not already included

// Function declarations (updated to accept VulkanDevice instead of VkDevice and other raw Vulkan handles)
VkCommandBuffer beginSingleTimeCommands(VulkanDevice& vulkanDevice);
void endSingleTimeCommands(VulkanDevice& vulkanDevice, VkCommandBuffer commandBuffer);
void copyBuffer(VulkanDevice& vulkanDevice, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);