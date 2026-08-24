#include "VoronoiRuntime.hpp"

#include "spatial/SpatialOrder.hpp"
#include "voronoi/VoronoiPointRuntime.hpp"

#include <iostream>

void VoronoiRuntime::invalidateMaterialization() {
    voronoiReady = false;
    voxelGrid = VoxelGrid{};
    voxelGridBuilt = false;
    seedFlags.clear();
    seedPositions.clear();
    globalRemeshRuntimeModelIds.clear();
    globalRemeshPositions.clear();
    globalRemeshTriangleIndices.clear();
    globalRemeshSurfacePositions.clear();
    globalRemeshSurfaceTriangleIndices.clear();
    globalSdfPadding = 0.0f;
    isGlobal = false;
    nodeDomain = {};
}

void VoronoiRuntime::resetDomainRuntime() {
    if (domainRuntime) {
        domainRuntime->cleanup();
    }
    domainRuntime.reset();
}

void VoronoiRuntime::reorderSeeds() {
    if (seedPositions.empty()) {
        return;
    }

    auto oldToNew = computeMortonPermutation(seedPositions);
    if (oldToNew.empty()) {
        return;
    }

    const uint32_t nodeCount = static_cast<uint32_t>(seedPositions.size());

    // Reorder seedPositions
    std::vector<glm::vec4> newPositions(nodeCount);
    for (uint32_t oldIdx = 0; oldIdx < nodeCount; ++oldIdx) {
        newPositions[oldToNew[oldIdx]] = seedPositions[oldIdx];
    }
    seedPositions = std::move(newPositions);

    // Reorder seedFlags
    if (!seedFlags.empty() && seedFlags.size() == nodeCount) {
        std::vector<uint32_t> newFlags(nodeCount);
        for (uint32_t oldIdx = 0; oldIdx < nodeCount; ++oldIdx) {
            newFlags[oldToNew[oldIdx]] = seedFlags[oldIdx];
        }
        seedFlags = std::move(newFlags);
    }

}

void VoronoiRuntime::setPointGeometry(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CommandPool& renderCommandPool,
    uint64_t domainKey,
    const std::vector<glm::vec4>& positions,
    const std::array<glm::vec3, 8>& domainCorners) {
    invalidateMaterialization();
    resetDomainRuntime();

    if (positions.empty()) {
        return;
    }

    auto nextPointRuntime = std::make_unique<VoronoiPointRuntime>(
        vulkanDevice,
        memoryAllocator,
        domainKey,
        positions,
        std::vector<uint32_t>{},    // no boundary conditions for raw points
        std::vector<float>{},       // no fixed temperatures for raw points
        renderCommandPool);
    if (!nextPointRuntime->createVoronoiBuffers()) {
        std::cerr << "[VoronoiRuntime] Failed to create Voronoi buffers for point domain key="
                  << domainKey << std::endl;
        nextPointRuntime->cleanup();
        return;
    }

    domainRuntime = std::move(nextPointRuntime);
    setSeedPositions(positions, domainCorners);
}

void VoronoiRuntime::setSeedPositions(
    const std::vector<glm::vec4>& positions,
    const std::array<glm::vec3, 8>& domainCorners) {
    seedPositions = positions;
    pointDomainCorners = domainCorners;
    seedFlags.assign(positions.size(), 0u);
}

void VoronoiRuntime::setGlobalGeometry(
    const std::vector<uint32_t>& runtimeModelIds,
    const std::vector<std::vector<glm::vec3>>& positions,
    const std::vector<std::vector<uint32_t>>& triangleIndices,
    const std::vector<std::vector<glm::vec3>>& surfacePositions,
    const std::vector<std::vector<uint32_t>>& surfaceTriangleIndices,
    float sdfPadding) {
    globalRemeshRuntimeModelIds = runtimeModelIds;
    globalRemeshPositions = positions;
    globalRemeshTriangleIndices = triangleIndices;
    globalRemeshSurfacePositions = surfacePositions;
    globalRemeshSurfaceTriangleIndices = surfaceTriangleIndices;
    globalSdfPadding = sdfPadding;
    isGlobal = !runtimeModelIds.empty();
    voronoiReady = false;
    nodeDomain = {};
}

void VoronoiRuntime::clearGeometry() {
    invalidateMaterialization();
    resetDomainRuntime();
}

void VoronoiRuntime::setParams(float updatedCellSize, int updatedVoxelResolution) {
    if (cellSize == updatedCellSize && voxelResolution == updatedVoxelResolution) {
        return;
    }

    cellSize = updatedCellSize;
    voxelResolution = updatedVoxelResolution;
    invalidateMaterialization();
}

void VoronoiRuntime::markReady() {
    voronoiReady = true;
}

void VoronoiRuntime::cleanup() {
    invalidateMaterialization();
    resetDomainRuntime();
}
