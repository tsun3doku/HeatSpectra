#include "IntrinsicRenderer.hpp"

#include "mesh/remesher/SupportingHalfedge.hpp"
#include "mesh/remesher/iODT.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "scene/Model.hpp"
#include "util/File_utils.h"
#include "util/Structs.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_set>

IntrinsicRenderer::IntrinsicRenderer(VulkanDevice& device, MemoryAllocator& allocator, RuntimeIntrinsicCache& remeshResources, UniformBufferManager& uboManager, CommandPool& commandPool,
    VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex)
    : vulkanDevice(device), allocator(allocator), remeshResources(remeshResources), uniformBufferManager(uboManager), renderCommandPool(commandPool) {
    if (!initialize(renderPass, maxFramesInFlight, subpassIndex)) {
        std::cerr << "[IntrinsicRenderer] Failed to initialize renderer resources" << std::endl;
    }
}

IntrinsicRenderer::~IntrinsicRenderer() {
    cleanup();
}

bool IntrinsicRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex) {
    if (initialized) {
        cleanup();
    }

    if (!createWireframeTexture() ||
        !createSupportingHalfedgeDescriptorPool(maxFramesInFlight) ||
        !createSupportingHalfedgeDescriptorSetLayout() ||
        !createIntrinsicNormalsDescriptorPool(maxFramesInFlight) ||
        !createIntrinsicNormalsDescriptorSetLayout() ||
        !createIntrinsicVertexNormalsDescriptorPool(maxFramesInFlight) ||
        !createIntrinsicVertexNormalsDescriptorSetLayout() ||
        !createSupportingHalfedgePipeline(renderPass, subpassIndex) ||
        !createIntrinsicNormalsPipeline(renderPass, subpassIndex) ||
        !createIntrinsicVertexNormalsPipeline(renderPass, subpassIndex)) {
        cleanup();
        return false;
    }

    initialized = true;
    return true;
}

uint32_t IntrinsicRenderer::calculateMipLevels(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++levels;
    }
    return levels;
}

bool IntrinsicRenderer::createWireframeTexture() {
    if (wireframeTextureSampler != VK_NULL_HANDLE ||
        wireframeTextureView != VK_NULL_HANDLE ||
        wireframeTextureImage != VK_NULL_HANDLE ||
        wireframeTextureMemory != VK_NULL_HANDLE) {
        return true;
    }

    const uint32_t width = 4096;
    const uint32_t height = 1;
    const float thickness = 1.5f;
    const VkFormat wireFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const uint32_t mipLevels = calculateMipLevels(width, height);

    std::vector<uint8_t> pixels;
    std::vector<VkBufferImageCopy> copyRegions;
    pixels.reserve(width * height * 4 * mipLevels);
    copyRegions.reserve(mipLevels);

    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    for (uint32_t level = 0; level < mipLevels; ++level) {
        const size_t levelOffset = pixels.size();
        const size_t levelSize = static_cast<size_t>(mipWidth) * static_cast<size_t>(mipHeight) * 4;
        pixels.resize(levelOffset + levelSize, 0);

        float mipWidthFloat = static_cast<float>(mipWidth);
        float levelThickness;
        if (thickness < mipWidthFloat / 3.0f) {
            levelThickness = thickness;
        } else {
            levelThickness = mipWidthFloat / 3.0f;
        }
        int integerThickness = static_cast<int>(std::floor(levelThickness / 2.0f));
        float fractionalThickness = std::fmod(levelThickness, 2.0f);

        for (uint32_t i = 0; i < mipWidth; ++i) {
            const size_t idx = levelOffset + static_cast<size_t>(i) * 4;
            pixels[idx + 0] = 0;
            pixels[idx + 1] = 0;
            pixels[idx + 2] = 0;
            pixels[idx + 3] = 0;

            if (i < static_cast<uint32_t>(integerThickness)) {
                pixels[idx + 3] = 255;
            } else if (i == static_cast<uint32_t>(integerThickness) && fractionalThickness > 0.0f) {
                pixels[idx + 3] = static_cast<uint8_t>(fractionalThickness * 255.0f);
            } else if (i >= mipWidth - static_cast<uint32_t>(integerThickness)) {
                pixels[idx + 3] = 255;
            } else if (i == mipWidth - 1 - static_cast<uint32_t>(integerThickness) && fractionalThickness > 0.0f) {
                pixels[idx + 3] = static_cast<uint8_t>(fractionalThickness * 255.0f);
            }
        }

        VkBufferImageCopy region{};
        region.bufferOffset = static_cast<VkDeviceSize>(levelOffset);
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mipWidth, mipHeight, 1};
        copyRegions.push_back(region);

        if (mipWidth > 1) {
            mipWidth /= 2;
        }
        if (mipHeight > 1) {
            mipHeight /= 2;
        }
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixels.size());
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    if (vulkanDevice.createBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBufferMemory,
            stagingBuffer) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create wireframe staging buffer" << std::endl;
        return false;
    }

    void* data = nullptr;
    if (vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data) != VK_SUCCESS || !data) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return false;
    }
    std::memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);

    if (createImage(
            vulkanDevice,
            width,
            height,
            wireFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            wireframeTextureImage,
            wireframeTextureMemory,
            VK_SAMPLE_COUNT_1_BIT,
            mipLevels) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return false;
    }

    if (transitionImageLayout(
            renderCommandPool,
            wireframeTextureImage,
            wireFormat,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            mipLevels) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return false;
    }

    {
        VkCommandBuffer commandBuffer = renderCommandPool.beginCommands();
        if (commandBuffer == VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
            return false;
        }
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            wireframeTextureImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copyRegions.size()),
            copyRegions.data());
        renderCommandPool.endCommands(commandBuffer);
    }

    if (transitionImageLayout(
            renderCommandPool,
            wireframeTextureImage,
            wireFormat,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            mipLevels) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        return false;
    }

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);

    wireframeTextureView = createImageView(
        vulkanDevice,
        wireframeTextureImage,
        wireFormat,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mipLevels);
    if (wireframeTextureView == VK_NULL_HANDLE) {
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels - 1);
    samplerInfo.anisotropyEnable = VK_FALSE;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &wireframeTextureSampler) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create wireframe sampler" << std::endl;
        return false;
    }

    return true;
}

bool IntrinsicRenderer::createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels * 10;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &supportingHalfedgeDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool IntrinsicRenderer::createSupportingHalfedgeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[11].descriptorCount = 1;
    bindings[11].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &supportingHalfedgeDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

void IntrinsicRenderer::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) {
        return;
    }

    if (perModelSupportingHalfedgeDescriptorSets.find(model) != perModelSupportingHalfedgeDescriptorSets.end()) {
        return;
    }

    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, supportingHalfedgeDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = supportingHalfedgeDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to allocate supporting halfedge descriptor sets" << std::endl;
        return;
    }

    perModelSupportingHalfedgeDescriptorSets[model] = descriptorSets;
}

void IntrinsicRenderer::updateDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (!model || !remesher) {
        return;
    }

    if (perModelSupportingHalfedgeDescriptorSets.find(model) == perModelSupportingHalfedgeDescriptorSets.end()) {
        allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelSupportingHalfedgeDescriptorSets.find(model);
    if (descriptorSetIt == perModelSupportingHalfedgeDescriptorSets.end()) {
        return;
    }

    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 12> descriptorWrites{};

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        VkBufferView bufferViews[10] = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView()
        };

        for (int j = 0; j < 10; j++) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = descriptorSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].dstArrayElement = 0;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }

        VkDescriptorImageInfo wireframeInfo{};
        wireframeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        wireframeInfo.imageView = wireframeTextureView;
        wireframeInfo.sampler = wireframeTextureSampler;

        descriptorWrites[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[11].dstSet = descriptorSets[i];
        descriptorWrites[11].dstBinding = 11;
        descriptorWrites[11].dstArrayElement = 0;
        descriptorWrites[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[11].descriptorCount = 1;
        descriptorWrites[11].pImageInfo = &wireframeInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

bool IntrinsicRenderer::createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicNormalsDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic normals descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool IntrinsicRenderer::createIntrinsicNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicNormalsDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic normals descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

void IntrinsicRenderer::allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) {
        return;
    }

    if (perModelIntrinsicNormalsDescriptorSets.find(model) != perModelIntrinsicNormalsDescriptorSets.end()) {
        return;
    }

    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicNormalsDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to allocate intrinsic normals descriptor sets" << std::endl;
        return;
    }

    perModelIntrinsicNormalsDescriptorSets[model] = descriptorSets;
}

void IntrinsicRenderer::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (!model || !remesher) {
        return;
    }

    if (perModelIntrinsicNormalsDescriptorSets.find(model) == perModelIntrinsicNormalsDescriptorSets.end()) {
        allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelIntrinsicNormalsDescriptorSets.find(model);
    if (descriptorSetIt == perModelIntrinsicNormalsDescriptorSets.end()) {
        return;
    }

    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }

    VkBuffer triangleBuffer = supportingHalfedge->getIntrinsicTriangleBuffer();
    VkDeviceSize triangleOffset = supportingHalfedge->getTriangleGeometryOffset();
    size_t triangleCount = supportingHalfedge->getTriangleCount();

    if (triangleBuffer == VK_NULL_HANDLE || triangleCount == 0) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        VkDescriptorBufferInfo triangleBufferInfo{};
        triangleBufferInfo.buffer = triangleBuffer;
        triangleBufferInfo.offset = triangleOffset;
        triangleBufferInfo.range = triangleCount * sizeof(IntrinsicTriangleData);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &triangleBufferInfo;

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

bool IntrinsicRenderer::createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicVertexNormalsDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool IntrinsicRenderer::createIntrinsicVertexNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicVertexNormalsDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

void IntrinsicRenderer::allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) {
        return;
    }

    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) != perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        return;
    }

    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicVertexNormalsDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicVertexNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to allocate intrinsic vertex normals descriptor sets" << std::endl;
        return;
    }

    perModelIntrinsicVertexNormalsDescriptorSets[model] = descriptorSets;
}

void IntrinsicRenderer::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (!model || !remesher) {
        return;
    }

    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelIntrinsicVertexNormalsDescriptorSets.find(model);
    if (descriptorSetIt == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        return;
    }

    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }

    VkBuffer vertexBuffer = supportingHalfedge->getIntrinsicVertexBuffer();
    VkDeviceSize vertexOffset = supportingHalfedge->getVertexGeometryOffset();
    size_t vertexCount = supportingHalfedge->getVertexCount();

    if (vertexBuffer == VK_NULL_HANDLE || vertexCount == 0) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        VkDescriptorBufferInfo vertexBufferInfo{};
        vertexBufferInfo.buffer = vertexBuffer;
        vertexBufferInfo.offset = vertexOffset;
        vertexBufferInfo.range = vertexCount * sizeof(IntrinsicVertexData);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &vertexBufferInfo;

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

bool IntrinsicRenderer::uploadPayloadState(Model* model, const IntrinsicMeshData& intrinsic, PayloadState& state) {
    if (!model) {
        return false;
    }

    releasePayloadState(state);

    auto uploadTexel = [this](const void* data, VkDeviceSize size, VkFormat format,
        VkBuffer& buffer, VkDeviceSize& offset, VkBufferView& view, const char* label) -> bool {
            if (size == 0) {
                return false;
            }
            if (createTexelBuffer(allocator, vulkanDevice, data, size, format, buffer, offset, view) != VK_SUCCESS) {
                std::cerr << "[IntrinsicRenderer] Failed to upload " << label << " buffer" << std::endl;
                return false;
            }
            return true;
        };

    if (!uploadTexel(intrinsic.supportingHalfedges.data(), intrinsic.supportingHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, state.bufferS, state.offsetS, state.viewS, "supporting-halfedge") ||
        !uploadTexel(intrinsic.supportingAngles.data(), intrinsic.supportingAngles.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, state.bufferA, state.offsetA, state.viewA, "supporting-angle") ||
        !uploadTexel(intrinsic.intrinsicHalfedges.data(), intrinsic.intrinsicHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT, state.bufferH, state.offsetH, state.viewH, "intrinsic-halfedge") ||
        !uploadTexel(intrinsic.intrinsicEdges.data(), intrinsic.intrinsicEdges.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT, state.bufferE, state.offsetE, state.viewE, "intrinsic-edge") ||
        !uploadTexel(intrinsic.intrinsicTriangles.data(), intrinsic.intrinsicTriangles.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, state.bufferT, state.offsetT, state.viewT, "intrinsic-triangle") ||
        !uploadTexel(intrinsic.intrinsicEdgeLengths.data(), intrinsic.intrinsicEdgeLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, state.bufferL, state.offsetL, state.viewL, "intrinsic-length") ||
        !uploadTexel(intrinsic.inputHalfedges.data(), intrinsic.inputHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT, state.bufferHInput, state.offsetHInput, state.viewHInput, "input-halfedge") ||
        !uploadTexel(intrinsic.inputEdges.data(), intrinsic.inputEdges.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT, state.bufferEInput, state.offsetEInput, state.viewEInput, "input-edge") ||
        !uploadTexel(intrinsic.inputTriangles.data(), intrinsic.inputTriangles.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, state.bufferTInput, state.offsetTInput, state.viewTInput, "input-triangle") ||
        !uploadTexel(intrinsic.inputEdgeLengths.data(), intrinsic.inputEdgeLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, state.bufferLInput, state.offsetLInput, state.viewLInput, "input-length")) {
        releasePayloadState(state);
        return false;
    }

    if (!intrinsic.triangles.empty()) {
        std::vector<IntrinsicTriangleData> gpuTriangles;
        gpuTriangles.reserve(intrinsic.triangles.size());
        for (const IntrinsicMeshTriangleData& triangle : intrinsic.triangles) {
            IntrinsicTriangleData gpuTriangle{};
            gpuTriangle.center = glm::vec3(triangle.center[0], triangle.center[1], triangle.center[2]);
            gpuTriangle.area = triangle.area;
            gpuTriangle.normal = glm::vec3(triangle.normal[0], triangle.normal[1], triangle.normal[2]);
            gpuTriangle.padding = 0.0f;
            gpuTriangles.push_back(gpuTriangle);
        }

        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                allocator,
                vulkanDevice,
                gpuTriangles.data(),
                gpuTriangles.size() * sizeof(IntrinsicTriangleData),
                state.intrinsicTriangleBuffer,
                state.triangleGeometryOffset,
                &mappedPtr) != VK_SUCCESS) {
            std::cerr << "[IntrinsicRenderer] Failed to upload intrinsic triangle geometry" << std::endl;
            releasePayloadState(state);
            return false;
        }
    }

    if (!intrinsic.vertices.empty()) {
        std::vector<IntrinsicVertexData> gpuVertices;
        gpuVertices.reserve(intrinsic.vertices.size());
        for (const IntrinsicMeshVertexData& vertex : intrinsic.vertices) {
            IntrinsicVertexData gpuVertex{};
            gpuVertex.position = glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]);
            gpuVertex.intrinsicVertexId = vertex.intrinsicVertexId;
            gpuVertex.normal = glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            gpuVertex.padding = 0.0f;
            gpuVertices.push_back(gpuVertex);
        }

        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                allocator,
                vulkanDevice,
                gpuVertices.data(),
                gpuVertices.size() * sizeof(IntrinsicVertexData),
                state.intrinsicVertexBuffer,
                state.vertexGeometryOffset,
                &mappedPtr) != VK_SUCCESS) {
            std::cerr << "[IntrinsicRenderer] Failed to upload intrinsic vertex geometry" << std::endl;
            releasePayloadState(state);
            return false;
        }
    }

    state.triangleCount = intrinsic.triangles.size();
    state.vertexCount = intrinsic.vertices.size();
    state.averageTriangleArea = 0.0f;
    for (const IntrinsicMeshTriangleData& triangle : intrinsic.triangles) {
        state.averageTriangleArea += triangle.area;
    }
    if (state.triangleCount > 0) {
        state.averageTriangleArea /= static_cast<float>(state.triangleCount);
    }
    state.uploaded = true;
    return true;
}

void IntrinsicRenderer::updatePayloadDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight) {
    if (!model || !state.uploaded) {
        return;
    }

    if (perModelSupportingHalfedgeDescriptorSets.find(model) == perModelSupportingHalfedgeDescriptorSets.end()) {
        allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelSupportingHalfedgeDescriptorSets.find(model);
    if (descriptorSetIt == perModelSupportingHalfedgeDescriptorSets.end()) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        std::array<VkWriteDescriptorSet, 12> descriptorWrites{};

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        VkBufferView bufferViews[10] = {
            state.viewS,
            state.viewA,
            state.viewH,
            state.viewE,
            state.viewT,
            state.viewL,
            state.viewHInput,
            state.viewEInput,
            state.viewTInput,
            state.viewLInput
        };

        for (int j = 0; j < 10; ++j) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = descriptorSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }

        VkDescriptorImageInfo wireframeInfo{};
        wireframeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        wireframeInfo.imageView = wireframeTextureView;
        wireframeInfo.sampler = wireframeTextureSampler;

        descriptorWrites[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[11].dstSet = descriptorSets[i];
        descriptorWrites[11].dstBinding = 11;
        descriptorWrites[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[11].descriptorCount = 1;
        descriptorWrites[11].pImageInfo = &wireframeInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void IntrinsicRenderer::updatePayloadNormalsDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight) {
    if (!model || !state.uploaded || state.intrinsicTriangleBuffer == VK_NULL_HANDLE) {
        return;
    }

    if (perModelIntrinsicNormalsDescriptorSets.find(model) == perModelIntrinsicNormalsDescriptorSets.end()) {
        allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelIntrinsicNormalsDescriptorSets.find(model);
    if (descriptorSetIt == perModelIntrinsicNormalsDescriptorSets.end()) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        VkDescriptorBufferInfo triangleBufferInfo{};
        triangleBufferInfo.buffer = state.intrinsicTriangleBuffer;
        triangleBufferInfo.offset = state.triangleGeometryOffset;
        triangleBufferInfo.range = state.triangleCount * sizeof(IntrinsicTriangleData);

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &triangleBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void IntrinsicRenderer::updatePayloadVertexNormalsDescriptorSetsForModel(Model* model, const PayloadState& state, uint32_t maxFramesInFlight) {
    if (!model || !state.uploaded || state.intrinsicVertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    auto descriptorSetIt = perModelIntrinsicVertexNormalsDescriptorSets.find(model);
    if (descriptorSetIt == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        return;
    }

    const auto& descriptorSets = descriptorSetIt->second;
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        VkDescriptorBufferInfo vertexBufferInfo{};
        vertexBufferInfo.buffer = state.intrinsicVertexBuffer;
        vertexBufferInfo.offset = state.vertexGeometryOffset;
        vertexBufferInfo.range = state.vertexCount * sizeof(IntrinsicVertexData);

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &vertexBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void IntrinsicRenderer::updatePayloadForModel(Model* model, const IntrinsicMeshData& intrinsic, uint32_t maxFramesInFlight) {
    if (!model) {
        return;
    }

    PayloadState& state = payloadStateByModel[model];
    if (!uploadPayloadState(model, intrinsic, state)) {
        releaseDescriptorSetsForModel(model);
        return;
    }

    updatePayloadDescriptorSetsForModel(model, state, maxFramesInFlight);
    updatePayloadNormalsDescriptorSetsForModel(model, state, maxFramesInFlight);
    updatePayloadVertexNormalsDescriptorSetsForModel(model, state, maxFramesInFlight);
}

void IntrinsicRenderer::releaseDescriptorSetsForModel(Model* model) {
    if (!model) {
        return;
    }

    auto freeSets = [this, model](VkDescriptorPool pool, auto& setMap) {
        auto it = setMap.find(model);
        if (it == setMap.end()) {
            return;
        }
        if (pool != VK_NULL_HANDLE && !it->second.empty()) {
            vkFreeDescriptorSets(
                vulkanDevice.getDevice(),
                pool,
                static_cast<uint32_t>(it->second.size()),
                it->second.data());
        }
        setMap.erase(it);
    };

    freeSets(supportingHalfedgeDescriptorPool, perModelSupportingHalfedgeDescriptorSets);
    freeSets(intrinsicNormalsDescriptorPool, perModelIntrinsicNormalsDescriptorSets);
    freeSets(intrinsicVertexNormalsDescriptorPool, perModelIntrinsicVertexNormalsDescriptorSets);

    auto payloadIt = payloadStateByModel.find(model);
    if (payloadIt != payloadStateByModel.end()) {
        releasePayloadState(payloadIt->second);
        payloadStateByModel.erase(payloadIt);
    }
}

void IntrinsicRenderer::pruneStaleModelResources(const ResourceManager& resourceManager) {
    std::unordered_set<Model*> liveModels;
    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        if (Model* model = const_cast<Model*>(resourceManager.getModelByID(modelId))) {
            liveModels.insert(model);
        }
    }

    std::vector<Model*> staleModels;
    staleModels.reserve(perModelSupportingHalfedgeDescriptorSets.size() +
        perModelIntrinsicNormalsDescriptorSets.size() +
        perModelIntrinsicVertexNormalsDescriptorSets.size());

    auto collectStale = [&](const auto& setMap) {
        for (const auto& [model, sets] : setMap) {
            (void)sets;
            if (!model || liveModels.find(model) == liveModels.end() ||
                payloadStateByModel.find(model) == payloadStateByModel.end()) {
                staleModels.push_back(model);
            }
        }
    };

    collectStale(perModelSupportingHalfedgeDescriptorSets);
    collectStale(perModelIntrinsicNormalsDescriptorSets);
    collectStale(perModelIntrinsicVertexNormalsDescriptorSets);
    for (const auto& [model, state] : payloadStateByModel) {
        (void)state;
        if (!model || liveModels.find(model) == liveModels.end()) {
            staleModels.push_back(model);
        }
    }

    std::sort(staleModels.begin(), staleModels.end());
    staleModels.erase(std::unique(staleModels.begin(), staleModels.end()), staleModels.end());
    for (Model* model : staleModels) {
        releaseDescriptorSetsForModel(model);
    }
}

void IntrinsicRenderer::releasePayloadState(PayloadState& state) {
    auto destroyView = [this](VkBufferView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyBufferView(vulkanDevice.getDevice(), view, nullptr);
            view = VK_NULL_HANDLE;
        }
    };
    destroyView(state.viewS);
    destroyView(state.viewA);
    destroyView(state.viewH);
    destroyView(state.viewE);
    destroyView(state.viewT);
    destroyView(state.viewL);
    destroyView(state.viewHInput);
    destroyView(state.viewEInput);
    destroyView(state.viewTInput);
    destroyView(state.viewLInput);

    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            allocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };
    freeBuffer(state.bufferS, state.offsetS);
    freeBuffer(state.bufferA, state.offsetA);
    freeBuffer(state.bufferH, state.offsetH);
    freeBuffer(state.bufferE, state.offsetE);
    freeBuffer(state.bufferT, state.offsetT);
    freeBuffer(state.bufferL, state.offsetL);
    freeBuffer(state.bufferHInput, state.offsetHInput);
    freeBuffer(state.bufferEInput, state.offsetEInput);
    freeBuffer(state.bufferTInput, state.offsetTInput);
    freeBuffer(state.bufferLInput, state.offsetLInput);
    freeBuffer(state.intrinsicTriangleBuffer, state.triangleGeometryOffset);
    freeBuffer(state.intrinsicVertexBuffer, state.vertexGeometryOffset);

    state.triangleCount = 0;
    state.vertexCount = 0;
    state.averageTriangleArea = 0.0f;
    state.uploaded = false;
}

bool IntrinsicRenderer::createSupportingHalfedgePipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    auto vertShaderCode = readFile("shaders/intrinsic_supporting_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_supporting_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_supporting_frag.spv");

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge vertex shader module" << std::endl;
        return false;
    }
    VkShaderModule geomShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, geomShaderCode, geomShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge geometry shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        return false;
    }
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge fragment shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo};

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    colorBlendAttachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &supportingHalfedgeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &supportingHalfedgePipelineLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge pipeline layout" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = supportingHalfedgePipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassIndex;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &supportingHalfedgePipeline) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create supporting halfedge pipeline" << std::endl;
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), supportingHalfedgePipelineLayout, nullptr);
        supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    return true;
}

bool IntrinsicRenderer::createIntrinsicNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    auto vertShaderCode = readFile("shaders/intrinsic_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_normals_frag.spv");

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic normals vertex shader module" << std::endl;
        return false;
    }
    VkShaderModule geomShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, geomShaderCode, geomShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic normals geometry shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        return false;
    }
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic normals fragment shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 2.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    colorBlendAttachments[1].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicNormalsPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic-normals pipeline layout" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicNormalsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassIndex;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicNormalsPipeline) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic-normals pipeline" << std::endl;
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicNormalsPipelineLayout, nullptr);
        intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    return true;
}

bool IntrinsicRenderer::createIntrinsicVertexNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    auto vertShaderCode = readFile("shaders/intrinsic_vertex_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_vertex_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_vertex_normals_frag.spv");

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals vertex shader module" << std::endl;
        return false;
    }
    VkShaderModule geomShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, geomShaderCode, geomShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals geometry shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        return false;
    }
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals fragment shader module" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 2.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    colorBlendAttachments[1].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicVertexNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicVertexNormalsPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals pipeline layout" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicVertexNormalsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassIndex;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicVertexNormalsPipeline) != VK_SUCCESS) {
        std::cerr << "[IntrinsicRenderer] Failed to create intrinsic vertex normals pipeline" << std::endl;
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicVertexNormalsPipelineLayout, nullptr);
        intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    return true;
}

void IntrinsicRenderer::renderSupportingHalfedges(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager) {
    if (!initialized || supportingHalfedgePipeline == VK_NULL_HANDLE) {
        return;
    }

    pruneStaleModelResources(resourceManager);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, supportingHalfedgePipeline);
    vkCmdSetDepthBias(commandBuffer, 0.1f, 0.0f, 0.1f);

    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        const Model* model = resourceManager.getModelByID(modelId);
        if (!model) {
            continue;
        }
        Model* mutableModel = const_cast<Model*>(model);
        auto payloadIt = payloadStateByModel.find(mutableModel);
        if (payloadIt == payloadStateByModel.end() || !payloadIt->second.uploaded) {
            continue;
        }

        auto it = perModelSupportingHalfedgeDescriptorSets.find(mutableModel);
        if (it == perModelSupportingHalfedgeDescriptorSets.end()) {
            continue;
        }

        const auto& modelDescriptorSets = it->second;
        if (currentFrame >= modelDescriptorSets.size()) {
            continue;
        }

        glm::mat4 modelMatrix = mutableModel->getModelMatrix();
        vkCmdPushConstants(commandBuffer, supportingHalfedgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);

        VkBuffer modelVertexBuffer = model->getVertexBuffer();
        VkDeviceSize modelVertexOffset = model->getVertexBufferOffset();
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer(), model->getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, supportingHalfedgePipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model->getIndices().size()), 1, 0, 0, 0);
    }

    vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);
}

void IntrinsicRenderer::renderIntrinsicNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager, float normalLength) {
    if (!initialized || intrinsicNormalsPipeline == VK_NULL_HANDLE) {
        return;
    }

    pruneStaleModelResources(resourceManager);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicNormalsPipeline);

    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        const Model* model = resourceManager.getModelByID(modelId);
        if (!model) {
            continue;
        }
        Model* mutableModel = const_cast<Model*>(model);
        auto payloadIt = payloadStateByModel.find(mutableModel);
        if (payloadIt == payloadStateByModel.end() || !payloadIt->second.uploaded) {
            continue;
        }
        const PayloadState& state = payloadIt->second;

        auto it = perModelIntrinsicNormalsDescriptorSets.find(mutableModel);
        if (it == perModelIntrinsicNormalsDescriptorSets.end()) {
            continue;
        }

        size_t triangleCount = state.triangleCount;
        if (triangleCount == 0) {
            continue;
        }

        const auto& modelDescriptorSets = it->second;
        if (currentFrame >= modelDescriptorSets.size()) {
            continue;
        }

        NormalPushConstant pushConstants{};
        pushConstants.modelMatrix = mutableModel->getModelMatrix();
        pushConstants.normalLength = normalLength;
        pushConstants.avgArea = state.averageTriangleArea;

        vkCmdPushConstants(commandBuffer, intrinsicNormalsPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicNormalsPipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(triangleCount), 1, 0, 0);
    }
}

void IntrinsicRenderer::renderIntrinsicVertexNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ResourceManager& resourceManager, float normalLength) {
    if (!initialized || intrinsicVertexNormalsPipeline == VK_NULL_HANDLE) {
        return;
    }

    pruneStaleModelResources(resourceManager);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicVertexNormalsPipeline);

    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        const Model* model = resourceManager.getModelByID(modelId);
        if (!model) {
            continue;
        }
        Model* mutableModel = const_cast<Model*>(model);
        auto payloadIt = payloadStateByModel.find(mutableModel);
        if (payloadIt == payloadStateByModel.end() || !payloadIt->second.uploaded) {
            continue;
        }
        const PayloadState& state = payloadIt->second;

        auto it = perModelIntrinsicVertexNormalsDescriptorSets.find(mutableModel);
        if (it == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
            continue;
        }

        size_t vertexCount = state.vertexCount;
        if (vertexCount == 0) {
            continue;
        }

        const auto& modelDescriptorSets = it->second;
        if (currentFrame >= modelDescriptorSets.size()) {
            continue;
        }

        NormalPushConstant pushConstants{};
        pushConstants.modelMatrix = mutableModel->getModelMatrix();
        pushConstants.normalLength = normalLength;
        pushConstants.avgArea = 0.0f;

        vkCmdPushConstants(commandBuffer, intrinsicVertexNormalsPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicVertexNormalsPipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertexCount), 1, 0, 0);
    }
}

void IntrinsicRenderer::cleanup() {
    if (supportingHalfedgePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), supportingHalfedgePipeline, nullptr);
        supportingHalfedgePipeline = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicNormalsPipeline, nullptr);
        intrinsicNormalsPipeline = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicVertexNormalsPipeline, nullptr);
        intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    }

    if (supportingHalfedgePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), supportingHalfedgePipelineLayout, nullptr);
        supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicNormalsPipelineLayout, nullptr);
        intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicVertexNormalsPipelineLayout, nullptr);
        intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;
    }

    if (supportingHalfedgeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), supportingHalfedgeDescriptorSetLayout, nullptr);
        supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), intrinsicNormalsDescriptorSetLayout, nullptr);
        intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), intrinsicVertexNormalsDescriptorSetLayout, nullptr);
        intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (supportingHalfedgeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), supportingHalfedgeDescriptorPool, nullptr);
        supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), intrinsicNormalsDescriptorPool, nullptr);
        intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), intrinsicVertexNormalsDescriptorPool, nullptr);
        intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    }

    if (wireframeTextureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice.getDevice(), wireframeTextureSampler, nullptr);
        wireframeTextureSampler = VK_NULL_HANDLE;
    }
    if (wireframeTextureView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice.getDevice(), wireframeTextureView, nullptr);
        wireframeTextureView = VK_NULL_HANDLE;
    }
    if (wireframeTextureImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice.getDevice(), wireframeTextureImage, nullptr);
        wireframeTextureImage = VK_NULL_HANDLE;
    }
    if (wireframeTextureMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), wireframeTextureMemory, nullptr);
        wireframeTextureMemory = VK_NULL_HANDLE;
    }

    perModelSupportingHalfedgeDescriptorSets.clear();
    perModelIntrinsicNormalsDescriptorSets.clear();
    perModelIntrinsicVertexNormalsDescriptorSets.clear();
    for (auto& [model, state] : payloadStateByModel) {
        (void)model;
        releasePayloadState(state);
    }
    payloadStateByModel.clear();

    initialized = false;
}
