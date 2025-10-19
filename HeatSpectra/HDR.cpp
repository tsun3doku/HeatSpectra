#include <stb_image.h> 

#include "hdr.hpp"

#include <iostream>
#include <stdexcept>
#include <iomanip>

HDR::HDR(VulkanDevice* device, CommandPool* cmdPool) 
    : vulkanDevice(device), renderCommandPool(cmdPool) {
    this->vulkanDevice = vulkanDevice;
    createHDRTextureImage(HDR_PATH);
    createCubemapImage();
    createHDRRenderPass();
    createHDRFrameBuffer();
}

void HDR::cleanup() {
    vkDestroySampler(vulkanDevice->getDevice(), envMapSampler, nullptr);
    vkDestroyImageView(vulkanDevice->getDevice(),envMapImageView, nullptr);

    vkDestroyImage(vulkanDevice->getDevice(), envMapImage, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), envMapImageMemory, nullptr);

    vkDestroyImageView(vulkanDevice->getDevice(), cubemapImageView, nullptr);
    vkDestroyImage(vulkanDevice->getDevice(), cubemapImage, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), cubemapImageMemory, nullptr);

    vkDestroyRenderPass(vulkanDevice->getDevice(), renderPass, nullptr);
    vkDestroyFramebuffer(vulkanDevice->getDevice(), framebuffer, nullptr);
}

void HDR::createHDRTextureImage(const std::string& filePath) {
    int texWidth, texHeight, texChannels;
    // Load the HDR image as float data (use stbi_loadf for HDR)
    float* pixels = stbi_loadf(filePath.c_str(), &texWidth, &texHeight, &texChannels, 0);

    if (!pixels) {
        throw std::runtime_error("Failed to load HDR texture image");
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4; // 4 channels of floats

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    stagingBuffer = vulkanDevice->createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory
    );

    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);

    std::cout << "Loaded HDR Image: " << filePath << "\n";
    std::cout << "Width: " << texWidth << ", Height: " << texHeight << ", Channels: " << texChannels << "\n";

    stbi_image_free(pixels);

    VkImageCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.extent.width = texWidth;
    createInfo.extent.height = texHeight;
    createInfo.extent.depth = 1;
    createInfo.mipLevels = 1;
    createInfo.arrayLayers = 1;
    createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;  // HDR format with 32-bit floating-point channels
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Create an HDR image
    createImage(*vulkanDevice, createInfo, envMapImage, envMapImageMemory);

    // Transition image to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL before copying the data
    transitionImageLayout(*renderCommandPool, envMapImage, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy the data from the staging buffer to the HDR texture image
    renderCommandPool->copyBufferToImage(stagingBuffer, envMapImage, static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight));

    // Transition image to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL after copying
    transitionImageLayout(*renderCommandPool, envMapImage, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Clean up the staging buffer
    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingBufferMemory, nullptr);

    // Create ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = envMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vulkanDevice->getDevice(), &viewInfo, nullptr, &envMapImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view!");
    }

    // Create Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    if (vkCreateSampler(vulkanDevice->getDevice(), &samplerInfo, nullptr, &envMapSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

void HDR::createImage(VulkanDevice& vulkanDevice, const VkImageCreateInfo& createInfo, VkImage& image, VkDeviceMemory& imageMemory) {
    if (vkCreateImage(vulkanDevice.getDevice(), &createInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(vulkanDevice.getDevice(), image, imageMemory, 0);
}

glm::vec3 HDR::sampleEnvironmentMap(const glm::vec3& direction, float roughness) {
    // Placeholder for environment map sampling logic
    // In real use, you'd compute the appropriate mipmap level, sample the texture, and apply IBL techniques

    glm::vec3 color = glm::vec3(0.0f);  // Placeholder for sampled color

    // Perform importance sampling or basic environment map sampling here
    // For simplicity, this just samples the environment map directly.

    return color;
}

// Pre-filter environment map for IBL (roughness levels)
void HDR::prefilterEnvMap(float roughness) {
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        float roughness = static_cast<float>(mip) / (mipLevels - 1);
        // Bind cubemap face as framebuffer, render filtered results
    }
}

void HDR::uploadHDRTextureData(float* data, VkDeviceMemory stagingBufferMemory, VkBuffer stagingBuffer, uint32_t texWidth, uint32_t texHeight) {
    VkDeviceSize imageSize = texWidth * texHeight * 4;  // RGBA

    void* stagingData;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, imageSize, 0, &stagingData);
    memcpy(stagingData, data, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);

    renderCommandPool->copyBufferToImage(stagingBuffer, envMapImage, texWidth, texHeight);
}

void HDR::createCubemapImage() {
    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageCreateInfo.extent.width = cubemapSize; 
    imageCreateInfo.extent.height = cubemapSize;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 6;  // Six faces of the cubemap
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    createImage(*vulkanDevice, imageCreateInfo, cubemapImage, cubemapImageMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cubemapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;  // Six faces of the cubemap

    if (vkCreateImageView(vulkanDevice->getDevice(), &viewInfo, nullptr, &cubemapImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap image view");
    }
}

void HDR::createHDRRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(vulkanDevice->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void HDR::createHDRFrameBuffer() {
    framebuffers.clear();
    for (uint32_t face = 0; face < 6; face++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &cubemapImageView;
        framebufferInfo.width = cubemapSize;
        framebufferInfo.height = cubemapSize;
        framebufferInfo.layers = 1; // Render one layer at a time

        VkFramebuffer framebuffer;
        if (vkCreateFramebuffer(vulkanDevice->getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer for cubemap face");
        }
        framebuffers.push_back(framebuffer);
    }
}

void HDR::renderToCubemap(Camera& camera, VkCommandBuffer commandBuffer) {
    for (uint32_t face = 0; face < 6; ++face) {
        VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[face]; // Use the framebuffer for this face
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = { cubemapSize, cubemapSize };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set appropriate view/projection matrices for the cubemap face
        glm::mat4 viewMatrix = getCubemapViewMatrix(face);
        glm::mat4 projectionMatrix = camera.getProjectionMatrix(0.5625);
        projectionMatrix[1][1] *= -1; // Vulkan Y-axis correction

        // Push view/projection matrices to the shader
        //pushMatricesToShader(commandBuffer, viewMatrix, projectionMatrix);

        // Bind pipeline and descriptor sets
        //vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, HDRPipeline);
        //vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, HDRPipelineLayout, 0, 1, &HDRDescriptorSet, 0, nullptr);

        // Draw environment
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);
    }
}

glm::mat4 HDR::getCubemapViewMatrix(uint32_t face) {
    static const glm::vec3 directions[6] = {
        {1.0f, 0.0f, 0.0f},  // +X
        {-1.0f, 0.0f, 0.0f}, // -X
        {0.0f, 1.0f, 0.0f},  // +Y
        {0.0f, -1.0f, 0.0f}, // -Y
        {0.0f, 0.0f, 1.0f},  // +Z
        {0.0f, 0.0f, -1.0f}, // -Z
    };

    static const glm::vec3 upVectors[6] = {
        {0.0f, -1.0f, 0.0f}, // For +X
        {0.0f, -1.0f, 0.0f}, // For -X
        {0.0f, 0.0f, 1.0f},  // For +Y
        {0.0f, 0.0f, -1.0f}, // For -Y
        {0.0f, -1.0f, 0.0f}, // For +Z
        {0.0f, -1.0f, 0.0f}, // For -Z
    };

    return glm::lookAt(glm::vec3(0.0f), directions[face], upVectors[face]);
}