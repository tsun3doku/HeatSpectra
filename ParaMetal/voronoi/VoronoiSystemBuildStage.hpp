#pragma once

#include "voronoi/VoronoiCandidate.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"
#include "heat/HeatGpuStructs.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

class MemoryAllocator;
class VulkanDevice;
class CommandPool;
class VoronoiRuntime;
class VoronoiNodeDomain;

namespace voronoi {
class KNNNeighbors;
class GlobalCutCell;
}

namespace heat {
class GlobalContactLevelSet;
}

class VoronoiSystemBuildStage {
  public:
    VoronoiSystemBuildStage(VulkanDevice &vulkanDevice, MemoryAllocator &memoryAllocator, CommandPool &commandPool);
    ~VoronoiSystemBuildStage();

    bool buildNodeDomain(VoronoiRuntime &runtime, uint32_t maxNeighbors);
    bool buildGlobalDomain(VoronoiRuntime &runtime);
    bool buildGlobalDisplayCandidates(VoronoiRuntime &runtime);

    uint32_t getCandidateNodeCount() const { return candidateNodeCount; }
    VkBuffer getCandidateNodeBuffer() const { return candidateNodeBuffer; }
    VkDeviceSize getCandidateNodeBufferOffset() const { return candidateNodeBufferOffset; }
    VkBuffer getCandidateNeighborIndicesBuffer() const { return candidateNeighborIndicesBuffer; }
    VkDeviceSize getCandidateNeighborIndicesBufferOffset() const { return candidateNeighborIndicesBufferOffset; }
    VkBuffer getSeedPositionBuffer() const { return seedPositionBuffer; }
    VkDeviceSize getSeedPositionBufferOffset() const { return seedPositionBufferOffset; }
    VkBuffer getNodeBuffer() const { return nodeBuffer; }
    VkDeviceSize getNodeBufferOffset() const { return nodeBufferOffset; }
    VkBuffer getCouplingBuffer() const { return couplingBuffer; }
    VkDeviceSize getCouplingBufferOffset() const { return couplingBufferOffset; }
    uint32_t getCouplingCount() const { return couplingCount; }

    const std::vector<uint32_t>& getGlobalFragmentInstanceIds() const { return globalFragmentInstanceIds; }
    const std::vector<uint32_t>& getGlobalFragmentSeedIds() const { return globalFragmentSeedIds; }
    const std::vector<float>& getGlobalSurfaceBoundaryAreas() const { return globalSurfaceBoundaryAreas; }
    const std::vector<float>& getGlobalFragmentVolumes() const { return globalFragmentVolumes; }
    const std::vector<uint32_t>& getGlobalFaceInstanceIds() const { return globalFaceInstanceIds; }
    const std::vector<uint32_t>& getGlobalFaceFragmentA() const { return globalFaceFragmentA; }
    const std::vector<uint32_t>& getGlobalFaceFragmentB() const { return globalFaceFragmentB; }
    const std::vector<float>& getGlobalFaceAreas() const { return globalFaceAreas; }
    const std::vector<uint32_t>& getGlobalCutFaceFragmentA() const { return globalCutFaceFragmentA; }
    const std::vector<uint32_t>& getGlobalCutFaceFragmentB() const { return globalCutFaceFragmentB; }
    const std::vector<float>& getGlobalCutFaceAreas() const { return globalCutFaceAreas; }
    const std::vector<float>& getGlobalCutFaceGaps() const { return globalCutFaceGaps; }
    const std::vector<uint32_t>& getGlobalInstanceFragmentCounts() const { return globalInstanceFragmentCounts; }
    const std::vector<float>& getGlobalSdfValues() const { return globalSdfValues; }
    const std::vector<uint32_t>& getGlobalSdfRuntimeModelIds() const { return globalSdfRuntimeModelIds; }
    glm::vec3 getGlobalSdfGridMin() const { return globalSdfGridMin; }
    glm::ivec3 getGlobalSdfGridDim() const { return globalSdfGridDim; }
    float getGlobalSdfCellSize() const { return globalSdfCellSize; }
    const std::vector<glm::vec4>& getGlobalSeedPositions() const { return globalSeedPositions; }
    const std::array<glm::vec3, 8>& getGlobalDomainCorners() const { return globalDomainCorners; }

    const voronoi::VoronoiCandidate* getCandidate() const { return candidate.get(); }
    const std::vector<uint32_t>& getGlobalDisplayRuntimeModelIds() const {
        static const std::vector<uint32_t> empty;
        return candidate ? candidate->getRuntimeModelIds() : empty;
    }
    const std::vector<uint32_t>& getGlobalDisplayFaceCounts() const {
        static const std::vector<uint32_t> empty;
        return candidate ? candidate->getFaceCounts() : empty;
    }
    const std::vector<VkBuffer>& getGlobalDisplayVertexBuffers() const {
        static const std::vector<VkBuffer> empty;
        return candidate ? candidate->getVertexBuffers() : empty;
    }
    const std::vector<VkDeviceSize>& getGlobalDisplayVertexBufferOffsets() const {
        static const std::vector<VkDeviceSize> empty;
        return candidate ? candidate->getVertexBufferOffsets() : empty;
    }
    const std::vector<VkBuffer>& getGlobalDisplayFaceIndexBuffers() const {
        static const std::vector<VkBuffer> empty;
        return candidate ? candidate->getFaceIndexBuffers() : empty;
    }
    const std::vector<VkDeviceSize>& getGlobalDisplayFaceIndexBufferOffsets() const {
        static const std::vector<VkDeviceSize> empty;
        return candidate ? candidate->getFaceIndexBufferOffsets() : empty;
    }
    const std::vector<VkBuffer>& getGlobalDisplayCandidateBuffers() const {
        static const std::vector<VkBuffer> empty;
        return candidate ? candidate->getCandidateBuffers() : empty;
    }
    const std::vector<VkDeviceSize>& getGlobalDisplayCandidateBufferOffsets() const {
        static const std::vector<VkDeviceSize> empty;
        return candidate ? candidate->getCandidateBufferOffsets() : empty;
    }

    const std::vector<VkImageView>& getGlobalSdfImageViews() const;
    const std::vector<VkImageView>& getGlobalPsiImageViews() const;
    VkSampler getGlobalSdfSampler() const;

    VkBuffer getContactRegionBuffer() const;
    VkDeviceSize getContactRegionBufferOffset() const;
    VkDeviceSize getContactRegionBufferSize() const;
    VkBuffer getIndirectDrawBuffer() const;
    VkDeviceSize getIndirectDrawBufferOffset() const;

    void cleanupResources();
    void cleanup();

  private:
    bool uploadCandidateInputs(const std::vector<glm::vec4> &positions, const std::vector<uint32_t> &neighborIndices);
    bool uploadCandidateNodes(const std::vector<voronoi::Node> &nodes);
    bool buildPointTopology(const std::vector<glm::vec4> &positions,
                            const std::vector<uint32_t> &neighborIndices,
                            uint32_t neighborCount,
                            std::vector<voronoi::Node> &nodes,
                            std::vector<voronoi::NodeCoupling> &couplings,
                            std::vector<float> &surfacePatchAreas);
    bool compactAndUploadNodeDomain(
        VoronoiRuntime &runtime,
        const std::vector<voronoi::Node> &candidateNodes,
        const std::vector<voronoi::NodeCoupling> &candidateCouplings,
        const std::vector<float> &surfacePatchAreas);
    bool uploadNodeDomainBuffers(const VoronoiNodeDomain &nodeDomain);
    void extractMeshTriangles(const std::vector<glm::vec3> &positions, const std::vector<uint32_t> &indices,
                              std::vector<std::array<glm::vec4, 3>> &outMeshTriangles) const;

    VulkanDevice &vulkanDevice;
    MemoryAllocator &memoryAllocator;
    CommandPool &commandPool;

    uint32_t candidateNodeCount = 0;
    VkBuffer candidateNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize candidateNodeBufferOffset = 0;
    VkBuffer candidateNeighborIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize candidateNeighborIndicesBufferOffset = 0;
    VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedPositionBufferOffset = 0;
    VkBuffer nodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize nodeBufferOffset = 0;
    VkBuffer couplingBuffer = VK_NULL_HANDLE;
    VkDeviceSize couplingBufferOffset = 0;
    uint32_t couplingCount = 0;

    std::vector<uint32_t> globalFragmentInstanceIds;
    std::vector<uint32_t> globalFragmentSeedIds;
    std::vector<float> globalSurfaceBoundaryAreas;
    std::vector<float> globalFragmentVolumes;
    std::vector<uint32_t> globalFaceInstanceIds;
    std::vector<uint32_t> globalFaceFragmentA;
    std::vector<uint32_t> globalFaceFragmentB;
    std::vector<float> globalFaceAreas;
    std::vector<uint32_t> globalCutFaceFragmentA;
    std::vector<uint32_t> globalCutFaceFragmentB;
    std::vector<float> globalCutFaceAreas;
    std::vector<float> globalCutFaceGaps;
    std::vector<uint32_t> globalInstanceFragmentCounts;
    std::vector<float> globalSdfValues;
    std::vector<uint32_t> globalSdfRuntimeModelIds;
    glm::vec3 globalSdfGridMin{};
    glm::ivec3 globalSdfGridDim{};
    float globalSdfCellSize = 0.0f;
    std::vector<glm::vec4> globalSeedPositions;
    std::array<glm::vec3, 8> globalDomainCorners{};

    std::unique_ptr<voronoi::KNNNeighbors> knn;
    std::unique_ptr<voronoi::GlobalCutCell> globalCutCell;
    std::unique_ptr<heat::GlobalContactLevelSet> contactLevelSet;
    std::unique_ptr<voronoi::VoronoiCandidate> candidate;
};
