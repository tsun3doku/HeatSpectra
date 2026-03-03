#include <vulkan/vulkan.h>
#include <stb_image.h>

#include "CommandBufferManager.hpp"
#include "util/file_utils.h"
#include "VulkanImage.hpp"
#include <iostream>

VkResult createImage(const VulkanDevice& vulkanDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory, VkSampleCountFlagBits samples, uint32_t mipLevels) {
    image = VK_NULL_HANDLE;
    imageMemory = VK_NULL_HANDLE;
    if (vulkanDevice.getDevice() == VK_NULL_HANDLE || width == 0 || height == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageCreateInfo imageInfo = createImageCreateInfo(width, height, format, tiling, usage, samples, mipLevels);
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VkResult createImageResult = vkCreateImage(vulkanDevice.getDevice(), &imageInfo, nullptr, &image);
    if (createImageResult != VK_SUCCESS) {
        return createImageResult;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    uint32_t memoryTypeIndex = UINT32_MAX;
    if (!vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, properties, memoryTypeIndex)) {
        vkDestroyImage(vulkanDevice.getDevice(), image, nullptr);
        image = VK_NULL_HANDLE;
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    const VkResult allocateMemoryResult = vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &imageMemory);
    if (allocateMemoryResult != VK_SUCCESS) {
        vkDestroyImage(vulkanDevice.getDevice(), image, nullptr);
        image = VK_NULL_HANDLE;
        return allocateMemoryResult;
    }

    const VkResult bindResult = vkBindImageMemory(vulkanDevice.getDevice(), image, imageMemory, 0);
    if (bindResult != VK_SUCCESS) {
        vkFreeMemory(vulkanDevice.getDevice(), imageMemory, nullptr);
        vkDestroyImage(vulkanDevice.getDevice(), image, nullptr);
        imageMemory = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        return bindResult;
    }

    return VK_SUCCESS;
}

VkResult transitionImageLayout(CommandPool& commandPool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
    (void)format;
    if (image == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // Start single time command buffer
    VkCommandBuffer commandBuffer = commandPool.beginCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // First transition: image data is being written
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Second transition: image data will be read in a shader
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // Apply the memory barrier
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // End and submit the command buffer
    commandPool.endCommands(commandBuffer);
    return VK_SUCCESS;
}

VkResult createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView& outImageView, uint32_t mipLevels) {
    outImageView = VK_NULL_HANDLE;
    if (vulkanDevice.getDevice() == VK_NULL_HANDLE || image == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    const VkResult viewResult = vkCreateImageView(vulkanDevice.getDevice(), &viewInfo, nullptr, &outImageView);
    return viewResult;
}

VkImageView createImageView(const VulkanDevice& vulkanDevice, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) {
    VkImageView imageView = VK_NULL_HANDLE;
    const VkResult result = createImageView(vulkanDevice, image, format, aspectFlags, imageView, mipLevels);
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanImage] createImageView failed with VkResult=" << result << std::endl;
    }
    return imageView;
}

VkImageCreateInfo createImageCreateInfo(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkSampleCountFlagBits samples, uint32_t mipLevels) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return imageInfo;
}

VkResult createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code, VkShaderModule& outShaderModule) {
    outShaderModule = VK_NULL_HANDLE;
    if (vulkanDevice.getDevice() == VK_NULL_HANDLE || code.empty()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    const VkResult shaderResult = vkCreateShaderModule(vulkanDevice.getDevice(), &createInfo, nullptr, &outShaderModule);
    return shaderResult;
}

VkShaderModule createShaderModule(const VulkanDevice& vulkanDevice, const std::vector<char>& code) {
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    const VkResult result = createShaderModule(vulkanDevice, code, shaderModule);
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanImage] createShaderModule failed with VkResult=" << result << std::endl;
    }
    return shaderModule;
}

VkResult createTextureImage(VulkanDevice& vulkanDevice, CommandPool& commandPool, VkImage& textureImage, VkDeviceMemory& textureImageMemory, const char* imagePath) {
    textureImage = VK_NULL_HANDLE;
    textureImageMemory = VK_NULL_HANDLE;
    if (!imagePath) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(imagePath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (texWidth <= 0 || texHeight <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    VkResult stagingBufferResult = vulkanDevice.createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory,
        stagingBuffer
    );
    if (stagingBufferResult != VK_SUCCESS) {
        stbi_image_free(pixels);
        return stagingBufferResult;
    }

    void* data = nullptr;
    const VkResult mapResult = vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    if (mapResult != VK_SUCCESS || !data) {
        stbi_image_free(pixels);
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return mapResult == VK_SUCCESS ? VK_ERROR_MEMORY_MAP_FAILED : mapResult;
    }
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);

    stbi_image_free(pixels);

    VkResult imageResult = createImage(
        vulkanDevice, texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        textureImage, textureImageMemory, VK_SAMPLE_COUNT_1_BIT);
    if (imageResult != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return imageResult;
    }

    VkResult transitionResult = transitionImageLayout(
        commandPool, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (transitionResult != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return transitionResult;
    }
    commandPool.copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight)); 
    transitionResult = transitionImageLayout(
        commandPool, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (transitionResult != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return transitionResult;
    }

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
    return VK_SUCCESS;
}

VkResult createTextureImage(VulkanDevice& vulkanDevice, CommandPool& commandPool, const std::string& texturePath, VkImage& textureImage, VkDeviceMemory& textureImageMemory) {
    return createTextureImage(vulkanDevice, commandPool, textureImage, textureImageMemory, texturePath.c_str());
}

VkResult createTextureImageView(const VulkanDevice& vulkanDevice, VkImage textureImage, VkImageView& outImageView) {
    return createImageView(vulkanDevice, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, outImageView);
}

VkResult createTextureSampler(const VulkanDevice& vulkanDevice, VkSampler& textureSampler) {
    textureSampler = VK_NULL_HANDLE;
    if (vulkanDevice.getDevice() == VK_NULL_HANDLE || vulkanDevice.getPhysicalDevice() == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    return vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &textureSampler);
}
