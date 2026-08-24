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

    bool isGlobalDomain() const { return isGlobal; }
    const std::vector<uint32_t>& getGlobalRemeshRuntimeModelIds() const { return globalRemeshRuntimeModelIds; }
    const std::vector<std::vector<glm::vec3>>& getGlobalRemeshPositions() const { return globalRemeshPositions; }
    const std::vector<std::vector<uint32_t>>& getGlobalRemeshTriangleIndices() const { return globalRemeshTriangleIndices; }
    const std::vector<std::vector<glm::vec3>>& getGlobalRemeshSurfacePositions() const { return globalRemeshSurfacePositions; }
    const std::vector<std::vector<uint32_t>>& getGlobalRemeshSurfaceTriangleIndices() const { return globalRemeshSurfaceTriangleIndices; }
    float getGlobalSdfPadding() const { return globalSdfPadding; }

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

    void reorderSeeds();
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
    void setGlobalGeometry(
        const std::vector<uint32_t>& runtimeModelIds,
        const std::vector<std::vector<glm::vec3>>& positions,
        const std::vector<std::vector<uint32_t>>& triangleIndices,
        const std::vector<std::vector<glm::vec3>>& surfacePositions,
        const std::vector<std::vector<uint32_t>>& surfaceTriangleIndices,
        float sdfPadding);

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
    std::vector<uint32_t> globalRemeshRuntimeModelIds;
    std::vector<std::vector<glm::vec3>> globalRemeshPositions;
    std::vector<std::vector<uint32_t>> globalRemeshTriangleIndices;
    std::vector<std::vector<glm::vec3>> globalRemeshSurfacePositions;
    std::vector<std::vector<uint32_t>> globalRemeshSurfaceTriangleIndices;
    float globalSdfPadding = 0.0f;
    bool isGlobal = false;
    VoronoiNodeDomain nodeDomain;

    bool voronoiReady = false;
};
