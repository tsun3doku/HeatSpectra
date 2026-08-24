#include "voronoi/GlobalSdfTexture.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanImage.hpp"

#include <iostream>
#include <cstring>

namespace voronoi {

GlobalSdfTexture::GlobalSdfTexture(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      commandPool(commandPool) {
}

GlobalSdfTexture::~GlobalSdfTexture() {
    cleanup();
}

void GlobalSdfTexture::cleanup() {
    if (sdfSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice.getDevice(), sdfSampler, nullptr);
        sdfSampler = VK_NULL_HANDLE;
    }

    for (VkImageView view : sdfImageViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
    }
    sdfImageViews.clear();

    for (size_t i = 0; i < sdfImages.size(); ++i) {
        if (sdfImages[i] != VK_NULL_HANDLE) vkDestroyImage(vulkanDevice.getDevice(), sdfImages[i], nullptr);
        if (i < sdfImageMemories.size() && sdfImageMemories[i] != VK_NULL_HANDLE) {
            memoryAllocator.freeImageMemory(sdfImageMemories[i]);
        }
    }
    sdfImages.clear();
    sdfImageMemories.clear();

    for (VkImageView view : psiImageViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
    }
    psiImageViews.clear();

    for (size_t i = 0; i < psiImages.size(); ++i) {
        if (psiImages[i] != VK_NULL_HANDLE) vkDestroyImage(vulkanDevice.getDevice(), psiImages[i], nullptr);
        if (i < psiImageMemories.size() && psiImageMemories[i] != VK_NULL_HANDLE) {
            memoryAllocator.freeImageMemory(psiImageMemories[i]);
        }
    }
    psiImages.clear();
    psiImageMemories.clear();
}

bool GlobalSdfTexture::upload3DTexture(
    const float* data,
    const glm::ivec3& gridDim,
    VkImage& outImage,
    VkDeviceMemory& outMemory,
    VkImageView& outView) {
    if (!data || gridDim.x < 1 || gridDim.y < 1 || gridDim.z < 1) return false;

    const size_t voxelCount = static_cast<size_t>(gridDim.x) * gridDim.y * gridDim.z;
    const VkDeviceSize byteSize = voxelCount * sizeof(float);

    VkResult imgRes = createImage3D(
        vulkanDevice, memoryAllocator,
        gridDim.x, gridDim.y, gridDim.z,
        VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outImage, outMemory);
    if (imgRes != VK_SUCCESS) {
        return false;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* mappedData = nullptr;
    if (createStagingBuffer(memoryAllocator, byteSize, stagingBuffer, stagingOffset, &mappedData) != VK_SUCCESS || !mappedData) {
        vkDestroyImage(vulkanDevice.getDevice(), outImage, nullptr);
        memoryAllocator.freeImageMemory(outMemory);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }

    std::memcpy(mappedData, data, static_cast<size_t>(byteSize));

    VkCommandBuffer cmd = commandPool.beginCommands();

    VkImageMemoryBarrier barrierToDst{};
    barrierToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToDst.srcAccessMask = 0;
    barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierToDst.image = outImage;
    barrierToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = stagingOffset;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageOffset = { 0, 0, 0 };
    copyRegion.imageExtent = { static_cast<uint32_t>(gridDim.x), static_cast<uint32_t>(gridDim.y), static_cast<uint32_t>(gridDim.z) };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier barrierToShader{};
    barrierToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToShader.image = outImage;
    barrierToShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

    commandPool.endCommands(cmd);
    freeBuffer(memoryAllocator, stagingBuffer, stagingOffset);

    if (createImageView3D(vulkanDevice, outImage, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, outView) != VK_SUCCESS) {
        vkDestroyImage(vulkanDevice.getDevice(), outImage, nullptr);
        memoryAllocator.freeImageMemory(outMemory);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool GlobalSdfTexture::upload(
    const std::vector<float>& sdfValues,
    uint32_t channelCount,
    const glm::ivec3& gridDim,
    const std::vector<std::pair<uint32_t, uint32_t>>& activePairs) {
    cleanup();

    const size_t voxelsPerChannel = static_cast<size_t>(gridDim.x) * gridDim.y * gridDim.z;
    if (channelCount == 0 || voxelsPerChannel == 0 || sdfValues.size() < voxelsPerChannel * channelCount) {
        return false;
    }

    // 1. Create linear sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &sdfSampler) != VK_SUCCESS) {
        std::cerr << "[GlobalSdfTexture] Failed to create 3D SDF sampler." << std::endl;
        return false;
    }

    // 2. Upload SDF textures
    for (uint32_t ch = 0; ch < channelCount; ++ch) {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        if (!upload3DTexture(sdfValues.data() + ch * voxelsPerChannel, gridDim, image, mem, view)) {
            std::cerr << "[GlobalSdfTexture] Failed to upload 3D SDF channel " << ch << std::endl;
            cleanup();
            return false;
        }
        sdfImages.push_back(image);
        sdfImageMemories.push_back(mem);
        sdfImageViews.push_back(view);
    }

    // 3. Generate & upload 3D Psi (sdfA - sdfB) textures
    for (const auto& [pA, pB] : activePairs) {
        if (pA >= channelCount || pB >= channelCount) continue;

        std::vector<float> psiValues(voxelsPerChannel);
        const float* valA = sdfValues.data() + pA * voxelsPerChannel;
        const float* valB = sdfValues.data() + pB * voxelsPerChannel;
        for (size_t v = 0; v < voxelsPerChannel; ++v) {
            psiValues[v] = valA[v] - valB[v];
        }

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        if (!upload3DTexture(psiValues.data(), gridDim, image, mem, view)) {
            std::cerr << "[GlobalSdfTexture] Failed to upload 3D Psi for pair (" << pA << ", " << pB << ")" << std::endl;
            cleanup();
            return false;
        }
        psiImages.push_back(image);
        psiImageMemories.push_back(mem);
        psiImageViews.push_back(view);
    }

    return true;
}

} // namespace voronoi
