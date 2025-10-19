#pragma once

#include "VulkanDevice.hpp"
#include "CommandBufferManager.hpp"  // CommandPool class is defined here

void createImage(const VulkanDevice& vulkanDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, 
	VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkSampleCountFlagBits samples);
void transitionImageLayout(CommandPool& commandPool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

VkImageView createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
VkImageCreateInfo createImageCreateInfo(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkSampleCountFlagBits samples);
VkShaderModule createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code);

void createTextureImage(VulkanDevice& vulkanDevice, CommandPool& commandPool, const std::string& texturePath, VkImage& textureImage, VkDeviceMemory& textureImageMemory);
void createTextureImageView(VulkanDevice& vulkanDevice, VkImage textureImage);
void createTextureSampler(const VulkanDevice& vulkanDevice, VkSampler textureSampler);