#pragma once

// Function declarations
void createImage(const VulkanDevice& vulkanDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
void transitionImageLayout(VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
void transitionGbufferLayout(VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
VkImageView createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
VkImageCreateInfo createImageCreateInfo(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage);
VkShaderModule createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code);