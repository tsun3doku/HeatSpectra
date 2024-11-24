#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanDevice.hpp" 

VkCommandBuffer beginSingleTimeCommands(VulkanDevice& vulkanDevice);
void endSingleTimeCommands(VulkanDevice& vulkanDevice, VkCommandBuffer commandBuffer);
void copyBuffer(VulkanDevice& vulkanDevice, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);