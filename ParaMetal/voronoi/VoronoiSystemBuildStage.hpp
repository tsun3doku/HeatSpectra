#pragma once

#include "voronoi/VoronoiGpuStructs.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

class MemoryAllocator;
class VoronoiModelRuntime;
class VulkanDevice;
class CommandPool;
class VoronoiRuntime;
class VoronoiNodeDomain;
namespace voronoi { class KNNNeighbors; class RVD; }

class VoronoiSystemBuildStage {
  public:
    VoronoiSystemBuildStage(VulkanDevice &vulkanDevice, MemoryAllocator &memoryAllocator, CommandPool &commandPool);
    ~VoronoiSystemBuildStage();

    bool prepareDomainGeometry(VoronoiRuntime &runtime, float cellSize, int voxelResolution) const;
    bool buildNodeDomain(VoronoiRuntime &runtime, uint32_t maxNeighbors);

    void cleanupResources();
    void cleanup();

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
    VkBuffer getOccupancyPointBuffer() const { return occupancyPointBuffer; }
    VkDeviceSize getOccupancyPointBufferOffset() const { return occupancyPointBufferOffset; }
    uint32_t getOccupancyPointCount() const { return occupancyPointCount; }

  private:
    bool uploadCandidateInputs(const std::vector<glm::vec4> &positions,
                               const std::vector<uint32_t> &neighborIndices);
    bool uploadCandidateNodes(const std::vector<voronoi::Node> &nodes);
    bool buildPointTopology(VoronoiRuntime &runtime,
                            const std::vector<uint32_t> &neighborIndices,
                            uint32_t neighborCount,
                            std::vector<voronoi::Node> &nodes,
                            std::vector<voronoi::NodeCoupling> &couplings,
                            std::vector<float> &surfacePatchAreas);
    bool buildMeshTopology(VoronoiRuntime &runtime,
                           voronoi::KNNNeighbors &neighbors,
                           std::vector<voronoi::Node> &nodes,
                           std::vector<voronoi::NodeCoupling> &couplings,
                           std::vector<float> &surfacePatchAreas);
    bool compactAndUploadNodeDomain(
        VoronoiRuntime &runtime,
        const std::vector<voronoi::Node> &candidateNodes,
        const std::vector<voronoi::NodeCoupling> &candidateCouplings,
        const std::vector<float> &surfacePatchAreas);
    bool uploadNodeDomainBuffers(const VoronoiNodeDomain &nodeDomain);
    bool rebuildOccupancyPointBuffer(VoronoiRuntime &runtime);
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
    VkBuffer occupancyPointBuffer = VK_NULL_HANDLE;
    VkDeviceSize occupancyPointBufferOffset = 0;
    uint32_t occupancyPointCount = 0;
    std::unique_ptr<voronoi::RVD> rvd;
    std::unique_ptr<voronoi::KNNNeighbors> knn;
};
