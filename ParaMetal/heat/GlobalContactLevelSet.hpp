#pragma once

#include "vulkan/VulkanExternalBuffer.hpp"
#include "cuda/CudaExternalBuffer.hpp"
#include "cuda/CudaBuffer.cuh"
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

namespace voronoi {
namespace globalsdf {
struct BuildResult;
}
namespace globalcutcell {
struct BuildResult;
}
class GlobalSdfTexture;
} // namespace voronoi

namespace heat {

class GlobalContactRegion;

class GlobalContactLevelSet {
public:
    GlobalContactLevelSet(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~GlobalContactLevelSet();

    GlobalContactLevelSet(const GlobalContactLevelSet&) = delete;
    GlobalContactLevelSet& operator=(const GlobalContactLevelSet&) = delete;

    bool build(
        const voronoi::globalsdf::BuildResult& sdfResult,
        const voronoi::globalcutcell::BuildResult& cutResult,
        uint32_t channelCount,
        const glm::vec3& gridMin,
        const glm::ivec3& gridDim,
        float spacing);

    void cleanup();

    const std::vector<VkImageView>& getSdfImageViews() const;
    const std::vector<VkImageView>& getPsiImageViews() const;
    VkSampler getSampler() const;

    VkBuffer getContactRegionBuffer() const { return contactRegionExtBuffer.getBuffer(); }
    VkDeviceSize getContactRegionBufferOffset() const { return 0; }
    VkDeviceSize getContactRegionBufferSize() const { return contactRegionExtBuffer.getSize(); }

    VkBuffer getIndirectDrawBuffer() const { return indirectDrawExtBuffer.getBuffer(); }
    VkDeviceSize getIndirectDrawBufferOffset() const { return 0; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;

    std::unique_ptr<voronoi::GlobalSdfTexture> globalSdfTexture;
    std::unique_ptr<GlobalContactRegion> contactRegion;

    VulkanExternalBuffer contactRegionExtBuffer;
    CudaExternalBuffer contactRegionCudaBuffer;
    VulkanExternalBuffer indirectDrawExtBuffer;
    CudaExternalBuffer indirectDrawCudaBuffer;

    cudaUtils::CudaBuffer<uint32_t> d_pairToPsi;
};

} // namespace heat
