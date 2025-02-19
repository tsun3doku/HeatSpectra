#pragma once

#include "VulkanDevice.hpp" 

VkCommandBuffer beginSingleTimeCommands(VulkanDevice& vulkanDevice);
void endSingleTimeCommands(VulkanDevice& vulkanDevice, VkCommandBuffer commandBuffer);
void copyBuffer(VulkanDevice& device, VkBuffer srcBuffer, VkDeviceSize srcOffset, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size);
void copyBufferToImage(VulkanDevice& vulkanDevice, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
