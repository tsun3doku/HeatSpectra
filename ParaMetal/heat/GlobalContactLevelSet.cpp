#include "heat/GlobalContactLevelSet.hpp"
#include "heat/cuda/GlobalContactRegion.cuh"
#include "voronoi/GlobalSdfTexture.hpp"
#include "voronoi/cuda/GlobalSdf.cuh"
#include "voronoi/cuda/GlobalCutCell.cuh"
#include "cuda/CudaVulkanDevice.hpp"
#include "vulkan/VulkanDevice.hpp"
#include <algorithm>
#include <iostream>

namespace heat {

GlobalContactLevelSet::GlobalContactLevelSet(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      commandPool(commandPool) {
    contactRegion = std::make_unique<GlobalContactRegion>();
}

GlobalContactLevelSet::~GlobalContactLevelSet() {
    cleanup();
}

void GlobalContactLevelSet::cleanup() {
    contactRegionCudaBuffer.cleanup();
    indirectDrawCudaBuffer.cleanup();
    contactRegionExtBuffer.cleanup();
    indirectDrawExtBuffer.cleanup();

    if (contactRegion) {
        contactRegion->cleanup();
    }
    d_pairToPsi.reset();

    globalSdfTexture.reset();
}

const std::vector<VkImageView>& GlobalContactLevelSet::getSdfImageViews() const {
    static const std::vector<VkImageView> empty;
    return globalSdfTexture ? globalSdfTexture->getSdfImageViews() : empty;
}

const std::vector<VkImageView>& GlobalContactLevelSet::getPsiImageViews() const {
    static const std::vector<VkImageView> empty;
    return globalSdfTexture ? globalSdfTexture->getPsiImageViews() : empty;
}

VkSampler GlobalContactLevelSet::getSampler() const {
    return globalSdfTexture ? globalSdfTexture->getSampler() : VK_NULL_HANDLE;
}

bool GlobalContactLevelSet::build(
    const voronoi::globalsdf::BuildResult& sdfResult,
    const voronoi::globalcutcell::BuildResult& cutResult,
    uint32_t channelCount,
    const glm::vec3& gridMin,
    const glm::ivec3& gridDim,
    float spacing) {
    cleanup();

    const auto& activePairs = cutResult.activePairs;

    // Upload 3D SDF and Psi textures
    globalSdfTexture = std::make_unique<voronoi::GlobalSdfTexture>(vulkanDevice, memoryAllocator, commandPool);
    if (!globalSdfTexture->upload(sdfResult.values, channelCount, gridDim, activePairs)) {
        std::cerr << "[GlobalContactLevelSet] GlobalSdfTexture upload failed." << std::endl;
        cleanup();
        return false;
    }

    // Upload pairToPsi mapping
    std::vector<uint32_t> pairToPsiHost(channelCount * channelCount, 0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(activePairs.size()); ++i) {
        const auto& pair = activePairs[i];
        if (pair.first < channelCount && pair.second < channelCount) {
            pairToPsiHost[pair.first * channelCount + pair.second] = i;
            pairToPsiHost[pair.second * channelCount + pair.first] = i;
        }
    }
    if (!d_pairToPsi.allocate(pairToPsiHost.size()) ||
        !d_pairToPsi.upload(pairToPsiHost.data(), pairToPsiHost.size())) {
        std::cerr << "[GlobalContactLevelSet] Failed to allocate/upload pairToPsi." << std::endl;
        cleanup();
        return false;
    }

    int cudaDevice = cudaVulkan::findDevice(vulkanDevice.getPhysicalDevice());
    if (cudaDevice < 0) {
        std::cerr << "[GlobalContactLevelSet] Failed to find matching CUDA device." << std::endl;
        cleanup();
        return false;
    }

    const uint32_t maxContactFaces = cutResult.contactFaceCount;
    if (maxContactFaces > 0 && cutResult.d_contactFaces) {
        constexpr uint32_t MaxContactClusters = 10000;
        const VkDeviceSize regionBufferSize = sizeof(ContactRegion) * MaxContactClusters;
        const VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);

        if (!contactRegionExtBuffer.initialize(vulkanDevice, regionBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !contactRegionCudaBuffer.ensureMapped(contactRegionExtBuffer, cudaDevice)) {
            std::cerr << "[GlobalContactLevelSet] Failed to initialize ContactRegion external buffer." << std::endl;
            cleanup();
            return false;
        }

        if (!indirectDrawExtBuffer.initialize(vulkanDevice, indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ||
            !indirectDrawCudaBuffer.ensureMapped(indirectDrawExtBuffer, cudaDevice)) {
            std::cerr << "[GlobalContactLevelSet] Failed to initialize IndirectDraw external buffer." << std::endl;
            cleanup();
            return false;
        }

        // Run ContactRegion builder 
        ContactRegionBuildInput regionInput{};
        regionInput.d_faces = cutResult.d_contactFaces;
        regionInput.faceCount = cutResult.contactFaceCount;
        regionInput.channelCount = channelCount;
        regionInput.gridMin = make_float3(gridMin.x, gridMin.y, gridMin.z);
        regionInput.tileSize = spacing * 8.0f;
        regionInput.tileDim = make_uint3(
            uint32_t(std::max(1, (gridDim.x + 7) / 8)),
            uint32_t(std::max(1, (gridDim.y + 7) / 8)),
            uint32_t(std::max(1, (gridDim.z + 7) / 8)));
        regionInput.d_pairToPsiChannel = d_pairToPsi.get();

        ContactRegionBuildOutput regionOutput{};
        regionOutput.d_regions = reinterpret_cast<ContactRegion*>(contactRegionCudaBuffer.getPointer());
        regionOutput.d_indirectCmd = reinterpret_cast<VkDrawIndirectCommand*>(indirectDrawCudaBuffer.getPointer());

        if (!contactRegion->build(regionInput, regionOutput)) {
            std::cerr << "[GlobalContactLevelSet] GlobalContactRegion::build failed." << std::endl;
            cleanup();
            return false;
        }
    }

    return true;
}

} // namespace heat
