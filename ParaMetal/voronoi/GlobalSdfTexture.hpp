#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <utility>
#include <cstdint>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

namespace voronoi {

class GlobalSdfTexture {
public:
    GlobalSdfTexture(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~GlobalSdfTexture();

    GlobalSdfTexture(const GlobalSdfTexture&) = delete;
    GlobalSdfTexture& operator=(const GlobalSdfTexture&) = delete;
    GlobalSdfTexture(GlobalSdfTexture&&) noexcept = default;
    GlobalSdfTexture& operator=(GlobalSdfTexture&&) noexcept = default;

    bool upload(
        const std::vector<float>& sdfValues,
        uint32_t channelCount,
        const glm::ivec3& gridDim,
        const std::vector<std::pair<uint32_t, uint32_t>>& activePairs);

    void cleanup();

    const std::vector<VkImageView>& getSdfImageViews() const { return sdfImageViews; }
    const std::vector<VkImageView>& getPsiImageViews() const { return psiImageViews; }
    VkSampler getSampler() const { return sdfSampler; }

private:
    bool upload3DTexture(
        const float* data,
        const glm::ivec3& gridDim,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;

    std::vector<VkImage> sdfImages;
    std::vector<VkDeviceMemory> sdfImageMemories;
    std::vector<VkImageView> sdfImageViews;

    std::vector<VkImage> psiImages;
    std::vector<VkDeviceMemory> psiImageMemories;
    std::vector<VkImageView> psiImageViews;

    VkSampler sdfSampler = VK_NULL_HANDLE;
};

} // namespace voronoi
