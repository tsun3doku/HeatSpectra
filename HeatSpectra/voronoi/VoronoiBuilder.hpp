#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "voronoi/VoronoiDomain.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"
#include "voronoi/VoronoiResources.hpp"

class MemoryAllocator;
class VoronoiGeoCompute;
class VoronoiModelRuntime;
class VulkanDevice;

class VoronoiBuilder {
public:
    VoronoiBuilder(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        VoronoiResources& resources);

    bool buildDomains(
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes,
        std::vector<VoronoiDomain>& receiverVoronoiDomains,
        float cellSize,
        int voxelResolution,
        uint32_t maxNeighbors) const;

    bool generateDiagram(
        std::vector<VoronoiDomain>& receiverVoronoiDomains,
        bool debugEnable,
        uint32_t maxNeighbors,
        VoronoiGeoCompute* voronoiGeoCompute);
    bool stageSurfaceMappings(
        std::vector<VoronoiDomain>& receiverVoronoiDomains) const;

    void setGhost(std::vector<VoronoiDomain>& receiverVoronoiDomains, bool fromVolumes);

private:
    bool tryCreateStorageBuffer(
        const char* label,
        const void* data,
        VkDeviceSize size,
        VkBuffer& buffer,
        VkDeviceSize& offset,
        void** mapped,
        bool hostVisible = true) const;
    bool createVoronoiGeometryBuffers(
        const std::vector<voronoi::Node>& initialNodes,
        const std::vector<glm::vec4>& seedPositions,
        const std::vector<uint32_t>& seedFlags,
        const std::vector<uint32_t>& neighborIndices,
        bool debugEnable,
        uint32_t maxNeighbors);
    bool buildGMLSInterfaceBuffer(uint32_t maxNeighbors);
    bool rebuildOccupancyPointBuffer(const std::vector<VoronoiDomain>& domains) const;

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    VoronoiResources& resources;
};
