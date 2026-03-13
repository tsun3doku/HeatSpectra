#include "VoronoiRenderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "scene/Model.hpp"
#include "util/Structs.hpp"
#include "vulkan/VulkanImage.hpp"
#include "util/file_utils.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

VoronoiRenderer::VoronoiRenderer(VulkanDevice& device, UniformBufferManager& uboManager, CommandPool& commandPool)
    : vulkanDevice(device), uniformBufferManager(uboManager), renderCommandPool(commandPool) {
}

VoronoiRenderer::~VoronoiRenderer() {
    cleanup();
}

uint32_t VoronoiRenderer::calculateMipLevels(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++levels;
    }
    return levels;
}

bool VoronoiRenderer::createWireframeTexture() {
    if (wireframeTextureSampler != VK_NULL_HANDLE ||
        wireframeTextureView != VK_NULL_HANDLE ||
        wireframeTextureImage != VK_NULL_HANDLE ||
        wireframeTextureMemory != VK_NULL_HANDLE) {
        return true;
    }

    const uint32_t width = 4096;
    const uint32_t height = 1;
    const float thickness = 0.5f;
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
        std::cerr << "VoronoiRenderer: Failed to create wireframe staging buffer" << std::endl;
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
        std::cerr << "VoronoiRenderer: Failed to create wireframe sampler" << std::endl;
        return false;
    }

    return true;
}

void VoronoiRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (initialized) {
        cleanup();
    }
    
    if (!createWireframeTexture() ||
        !createDescriptorSetLayout() ||
        !createDescriptorPool(maxFramesInFlight) ||
        !createDescriptorSets(maxFramesInFlight) ||
        !createPipeline(renderPass)) {
        cleanup();
        return;
    }
    
    initialized = true;
}

bool VoronoiRenderer::createDescriptorSetLayout() {
    // Binding 0: UBO 
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 2: Seed positions 
    VkDescriptorSetLayoutBinding seedLayoutBinding{};
    seedLayoutBinding.binding = 2;
    seedLayoutBinding.descriptorCount = 1;
    seedLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    seedLayoutBinding.pImmutableSamplers = nullptr;
    seedLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 4: Voronoi neighbors 
    VkDescriptorSetLayoutBinding neighborLayoutBinding{};
    neighborLayoutBinding.binding = 4;
    neighborLayoutBinding.descriptorCount = 1;
    neighborLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    neighborLayoutBinding.pImmutableSamplers = nullptr;
    neighborLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding candidateLayoutBinding{};
    candidateLayoutBinding.binding = 16;
    candidateLayoutBinding.descriptorCount = 1;
    candidateLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    candidateLayoutBinding.pImmutableSamplers = nullptr;
    candidateLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Supporting halfedge buffers for intrinsic walk
    VkDescriptorSetLayoutBinding supportingLayoutBinding{};
    supportingLayoutBinding.binding = 6;
    supportingLayoutBinding.descriptorCount = 1;
    supportingLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    supportingLayoutBinding.pImmutableSamplers = nullptr;
    supportingLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding supportingAngleLayoutBinding{};
    supportingAngleLayoutBinding.binding = 7;
    supportingAngleLayoutBinding.descriptorCount = 1;
    supportingAngleLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    supportingAngleLayoutBinding.pImmutableSamplers = nullptr;
    supportingAngleLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding halfedgeLayoutBinding{};
    halfedgeLayoutBinding.binding = 8;
    halfedgeLayoutBinding.descriptorCount = 1;
    halfedgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    halfedgeLayoutBinding.pImmutableSamplers = nullptr;
    halfedgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding edgeLayoutBinding{};
    edgeLayoutBinding.binding = 9;
    edgeLayoutBinding.descriptorCount = 1;
    edgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    edgeLayoutBinding.pImmutableSamplers = nullptr;
    edgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding triLayoutBinding{};
    triLayoutBinding.binding = 10;
    triLayoutBinding.descriptorCount = 1;
    triLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    triLayoutBinding.pImmutableSamplers = nullptr;
    triLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lengthLayoutBinding{};
    lengthLayoutBinding.binding = 11;
    lengthLayoutBinding.descriptorCount = 1;
    lengthLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    lengthLayoutBinding.pImmutableSamplers = nullptr;
    lengthLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding inputHalfedgeLayoutBinding{};
    inputHalfedgeLayoutBinding.binding = 12;
    inputHalfedgeLayoutBinding.descriptorCount = 1;
    inputHalfedgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    inputHalfedgeLayoutBinding.pImmutableSamplers = nullptr;
    inputHalfedgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding inputEdgeLayoutBinding{};
    inputEdgeLayoutBinding.binding = 13;
    inputEdgeLayoutBinding.descriptorCount = 1;
    inputEdgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    inputEdgeLayoutBinding.pImmutableSamplers = nullptr;
    inputEdgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding inputTriangleLayoutBinding{};
    inputTriangleLayoutBinding.binding = 14;
    inputTriangleLayoutBinding.descriptorCount = 1;
    inputTriangleLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    inputTriangleLayoutBinding.pImmutableSamplers = nullptr;
    inputTriangleLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding inputLengthLayoutBinding{};
    inputLengthLayoutBinding.binding = 15;
    inputLengthLayoutBinding.descriptorCount = 1;
    inputLengthLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    inputLengthLayoutBinding.pImmutableSamplers = nullptr;
    inputLengthLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding wireframeBinding{};
    wireframeBinding.binding = 17;
    wireframeBinding.descriptorCount = 1;
    wireframeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wireframeBinding.pImmutableSamplers = nullptr;
    wireframeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 15> bindings = { 
        uboLayoutBinding, seedLayoutBinding, neighborLayoutBinding,
        supportingLayoutBinding, supportingAngleLayoutBinding, halfedgeLayoutBinding, edgeLayoutBinding, triLayoutBinding, lengthLayoutBinding,
        inputHalfedgeLayoutBinding, inputEdgeLayoutBinding, inputTriangleLayoutBinding, inputLengthLayoutBinding,
        candidateLayoutBinding,
        wireframeBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "VoronoiRenderer: Failed to create descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiRenderer::createDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 3; 
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[2].descriptorCount = maxFramesInFlight * 10;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[3].descriptorCount = maxFramesInFlight;


    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "VoronoiRenderer: Failed to create descriptor pool" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiRenderer::createDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        descriptorSets.clear();
        std::cerr << "VoronoiRenderer: Failed to allocate descriptor sets" << std::endl;
        return false;
    }

    return true;
}

void VoronoiRenderer::updateDescriptors(uint32_t frameIndex,
    uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
    VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
    VkBufferView supportingHalfedgeView, VkBufferView supportingAngleView,
    VkBufferView halfedgeView, VkBufferView edgeView,
    VkBufferView triangleView, VkBufferView lengthView,
    VkBufferView inputHalfedgeView, VkBufferView inputEdgeView,
    VkBufferView inputTriangleView, VkBufferView inputLengthView,
    VkBuffer candidateBuffer, VkDeviceSize candidateOffset) {
    currentVertexCount = vertexCount;
    currentCandidateBuffer = candidateBuffer;
    
    if (frameIndex >= descriptorSets.size()) {
        return;
    }

    std::array<VkWriteDescriptorSet, 15> descriptorWrites{};

    // Binding 0: UBO
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBufferManager.getUniformBuffers()[frameIndex];
    uboInfo.offset = uniformBufferManager.getUniformBufferOffsets()[frameIndex];
    uboInfo.range = sizeof(UniformBufferObject);
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[frameIndex];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &uboInfo;

    // Binding 2: Seed positions
    VkDescriptorBufferInfo seedInfo{};
    seedInfo.buffer = seedBuffer;
    seedInfo.offset = seedOffset;
    seedInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[frameIndex];
    descriptorWrites[1].dstBinding = 2;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &seedInfo;
    
    // Binding 4: Voronoi neighbors
    VkDescriptorBufferInfo neighborInfo{};
    neighborInfo.buffer = neighborBuffer;
    neighborInfo.offset = neighborOffset;
    neighborInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSets[frameIndex];
    descriptorWrites[2].dstBinding = 4;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &neighborInfo;

    VkDescriptorBufferInfo candidateInfo{};
    candidateInfo.buffer = candidateBuffer;
    candidateInfo.offset = candidateOffset;
    candidateInfo.range = VK_WHOLE_SIZE;

    VkBufferView supportingViews[10] = {
        supportingHalfedgeView,
        supportingAngleView,
        halfedgeView,
        edgeView,
        triangleView,
        lengthView,
        inputHalfedgeView,
        inputEdgeView,
        inputTriangleView,
        inputLengthView
    };

    for (int i = 0; i < 10; ++i) {
        descriptorWrites[3 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3 + i].dstSet = descriptorSets[frameIndex];
        descriptorWrites[3 + i].dstBinding = 6 + i;
        descriptorWrites[3 + i].dstArrayElement = 0;
        descriptorWrites[3 + i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        descriptorWrites[3 + i].descriptorCount = 1;
        descriptorWrites[3 + i].pTexelBufferView = &supportingViews[i];
    }

    descriptorWrites[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[13].dstSet = descriptorSets[frameIndex];
    descriptorWrites[13].dstBinding = 16;
    descriptorWrites[13].dstArrayElement = 0;
    descriptorWrites[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[13].descriptorCount = 1;
    descriptorWrites[13].pBufferInfo = &candidateInfo;

    VkDescriptorImageInfo wireframeInfo{};
    wireframeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    wireframeInfo.imageView = wireframeTextureView;
    wireframeInfo.sampler = wireframeTextureSampler;

    descriptorWrites[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[14].dstSet = descriptorSets[frameIndex];
    descriptorWrites[14].dstBinding = 17;
    descriptorWrites[14].dstArrayElement = 0;
    descriptorWrites[14].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[14].descriptorCount = 1;
    descriptorWrites[14].pImageInfo = &wireframeInfo;

    vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

bool VoronoiRenderer::createPipeline(VkRenderPass renderPass) {
    std::vector<char> vertShaderCode;
    std::vector<char> geomShaderCode;
    std::vector<char> fragShaderCode;
    if (!readFile("shaders/voronoi_surface_vert.spv", vertShaderCode) ||
        !readFile("shaders/voronoi_surface_geom.spv", geomShaderCode) ||
        !readFile("shaders/voronoi_surface_frag.spv", fragShaderCode)) {
        std::cerr << "VoronoiRenderer: Failed to read shader files" << std::endl;
        return false;
    }
    
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule geomShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS ||
        createShaderModule(vulkanDevice, geomShaderCode, geomShaderModule) != VK_SUCCESS ||
        createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        if (vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        }
        if (geomShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        }
        if (fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        }
        std::cerr << "VoronoiRenderer: Failed to create shader modules" << std::endl;
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
    
    // Vertex input from Model class (extrinsic mesh)
    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto attributeDescriptions = Vertex::getVertexAttributes();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
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
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.minSampleShading = 0.0f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target.
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target disabled for Voronoi fill.
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GeometryPushConstant);  
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        std::cerr << "VoronoiRenderer: Failed to create pipeline layout" << std::endl;
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 2; // Grid subpass
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        std::cerr << "VoronoiRenderer: Failed to create graphics pipeline" << std::endl;
        return false;
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    return true;
}

void VoronoiRenderer::render(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
    VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
    uint32_t frameIndex, const glm::mat4& modelMatrix) {
    if (!initialized || currentCandidateBuffer == VK_NULL_HANDLE || frameIndex >= descriptorSets.size()) {
        return;
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, 
                           &descriptorSets[frameIndex], 0, nullptr);
    
    GeometryPushConstant pushConstant{};
    pushConstant.modelMatrix = modelMatrix;
    pushConstant.alpha = 1.0f; 
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                      0, sizeof(GeometryPushConstant), &pushConstant);
    
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {vertexOffset};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, indexOffset, VK_INDEX_TYPE_UINT32);
    
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void VoronoiRenderer::cleanup() {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
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
    
    descriptorSets.clear();
    initialized = false;
}
