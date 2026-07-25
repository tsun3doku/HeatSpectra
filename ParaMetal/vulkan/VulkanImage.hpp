#pragma once

#include "VulkanDevice.hpp"
#include "CommandBufferManager.hpp"
#include "MemoryAllocator.hpp"
#include <string>
#include <vector>

VkResult createImage(const VulkanDevice& vulkanDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkSampleCountFlagBits samples, uint32_t mipLevels = 1);
VkResult transitionImageLayout(CommandPool& commandPool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);
VkResult generateImageMipmaps(const VulkanDevice& vulkanDevice, CommandPool& commandPool, VkImage image,
    VkFormat format, int32_t width, int32_t height, uint32_t mipLevels);

VkResult createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView& outImageView, uint32_t mipLevels = 1);
VkImageView createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels = 1);
VkImageCreateInfo createImageCreateInfo(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkSampleCountFlagBits samples, uint32_t mipLevels = 1);
VkResult createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code, VkShaderModule& outShaderModule);
VkShaderModule createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code);

VkResult createTextureImage(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool, const std::string& texturePath, VkImage& textureImage, VkDeviceMemory& textureImageMemory, uint32_t mipLevels = 1);
VkResult createTextureImage(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool, VkImage& textureImage, VkDeviceMemory& textureImageMemory, const char* imagePath, uint32_t mipLevels = 1);
VkResult createTextureImage16(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool, const char* imagePath, VkImage& textureImage, VkDeviceMemory& textureImageMemory);
VkResult createTextureImageView(const VulkanDevice& vulkanDevice, VkImage textureImage, VkImageView& outImageView, uint32_t mipLevels = 1);
VkResult createTextureSampler(const VulkanDevice& vulkanDevice, VkSampler& textureSampler);
