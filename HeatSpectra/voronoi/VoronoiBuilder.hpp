#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "domain/VoronoiParams.hpp"
#include "voronoi/VoronoiDomain.hpp"
#include "voronoi/VoronoiResources.hpp"

class MemoryAllocator;
class PointRenderer;
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
        const VoronoiParams& params,
        uint32_t maxNeighbors) const;

    bool generateDiagram(
        std::vector<VoronoiDomain>& receiverVoronoiDomains,
        bool debugEnable,
        uint32_t maxNeighbors,
        VoronoiGeoCompute* voronoiGeoCompute,
        PointRenderer* pointRenderer);
    bool stageSurfaceMappings(
        std::vector<VoronoiDomain>& receiverVoronoiDomains,
        uint32_t maxNeighbors) const;

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
        const std::vector<VoronoiNode>& initialNodes,
        const std::vector<glm::vec4>& seedPositions,
        const std::vector<uint32_t>& seedFlags,
        const std::vector<uint32_t>& neighborIndices,
        bool debugEnable,
        uint32_t maxNeighbors);
    bool buildVoronoiNeighborBuffer(uint32_t maxNeighbors);
    void uploadOccupancyPoints(const std::vector<VoronoiDomain>& domains, PointRenderer* pointRenderer) const;

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    VoronoiResources& resources;
};
