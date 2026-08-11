#pragma once

#include "voronoi/VoronoiDomainRuntime.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"
#include "voronoi/VoronoiNodeDomain.hpp"
#include "spatial/VoxelGrid.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class CommandPool;
class MemoryAllocator;
class VulkanDevice;

class VoronoiRuntime {
public:
    bool isReady() const { return voronoiReady; }

    VoronoiDomainRuntime* getDomainRuntime() const { return domainRuntime.get(); }

    VoronoiNodeDomain& getNodeDomain() { return nodeDomain; }
    const VoronoiNodeDomain& getNodeDomain() const { return nodeDomain; }

    VoxelGrid& getVoxelGrid() { return voxelGrid; }
    const VoxelGrid& getVoxelGrid() const { return voxelGrid; }
    bool isVoxelGridBuilt() const { return voxelGridBuilt; }
    void setVoxelGridBuilt(bool built) { voxelGridBuilt = built; }
    std::vector<uint32_t>& getSeedFlags() { return seedFlags; }
    const std::vector<uint32_t>& getSeedFlags() const { return seedFlags; }

    std::vector<glm::vec4>& getSeedPositions() { return seedPositions; }
    const std::vector<glm::vec4>& getSeedPositions() const { return seedPositions; }
    const std::array<glm::vec3, 8>& getPointDomainCorners() const { return pointDomainCorners; }
    std::vector<std::array<glm::vec4, 3>>& getMeshTriangles() { return meshTriangles; }
    const std::vector<std::array<glm::vec4, 3>>& getMeshTriangles() const { return meshTriangles; }

    void reorderSeeds();
    void setMeshGeometry(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool,
        const std::vector<glm::vec3>& geometryPositions,
        const std::vector<uint32_t>& geometryTriangleIndices,
        const std::vector<voronoi::SurfaceVertex>& surfaceVertices,
        const std::vector<uint32_t>& surfaceTriangleIndices,
        uint32_t runtimeModelId,
        const glm::mat4& meshModelMatrix);
    void setPointGeometry(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        CommandPool& renderCommandPool,
        uint64_t domainKey,
        const std::vector<glm::vec4>& positions,
        const std::array<glm::vec3, 8>& domainCorners);
    void clearGeometry();
    void setParams(float cellSize, int voxelResolution);
    float getCellSize() const { return cellSize; }
    int getVoxelResolution() const { return voxelResolution; }
    void markReady();

    void setSeedPositions(
        const std::vector<glm::vec4>& positions,
        const std::array<glm::vec3, 8>& domainCorners);

    void cleanup();

private:
    void invalidateMaterialization();
    void resetDomainRuntime();

    std::unique_ptr<VoronoiDomainRuntime> domainRuntime;
    float cellSize = 0.005f;
    int voxelResolution = 128;

    VoxelGrid voxelGrid;
    bool voxelGridBuilt = false;
    std::vector<uint32_t> seedFlags;
    std::vector<glm::vec4> seedPositions;
    std::array<glm::vec3, 8> pointDomainCorners{};
    std::vector<std::array<glm::vec4, 3>> meshTriangles;
    VoronoiNodeDomain nodeDomain;

    bool voronoiReady = false;
};
