#pragma once

#include "heat/HeatGpuStructs.hpp"

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class HeatBoundaryRuntime {
public:
    struct Region {
        uint32_t id = 0;
        heat::BoundaryState state{};
        std::vector<uint32_t> nodeIds;
        std::vector<uint32_t> surfacePointIds;
    };

    bool configureRegions(
        const std::vector<Region>& regions,
        uint32_t nodeCount,
        uint32_t surfacePointCount,
        const std::vector<uint32_t>& surfaceNodeIds,
        const std::vector<float>& surfaceBoundaryAreas);
    bool buildBoundaryBuffers();
    bool createBuffers(VulkanDevice& device, MemoryAllocator& allocator, CommandPool& commandPool);
    bool setDirichletTemperatureC(uint32_t regionId, float temperatureC);
    bool setNeumannHeatFlux(uint32_t regionId, float heatFlux);
    bool setRobinState(uint32_t regionId, float ambientTemperatureC, float heatTransferCoefficient);
    bool setRobinTemperatureC(uint32_t regionId, float ambientTemperatureC);
    void uploadState(VkCommandBuffer commandBuffer);
    void cleanup(MemoryAllocator& allocator);

    VkBuffer getNodeBuffer() const { return nodeBuffer; }
    VkDeviceSize getNodeBufferOffset() const { return nodeBufferOffset; }
    VkBuffer getContributionBuffer() const { return contributionBuffer; }
    VkDeviceSize getContributionBufferOffset() const { return contributionBufferOffset; }
    VkBuffer getSurfaceIndexBuffer() const { return surfaceIndexBuffer; }
    VkDeviceSize getSurfaceIndexBufferOffset() const { return surfaceIndexBufferOffset; }
    VkBuffer getStateBuffer() const { return stateBuffer; }
    VkDeviceSize getStateBufferOffset() const { return stateBufferOffset; }
    const std::vector<uint32_t>& getDirichletNodeIds() const { return dirichletNodeIds; }
    const std::vector<uint32_t>& getSurfaceNodeIds() const { return surfaceNodeIds; }
    // legacy name: patch areas computed from clipped Voronoi surface geometry (mesh mode);
    // the global path feeds raw fragment surfaceBoundaryAreas from the cut-cell build.
    const std::vector<float>& getSurfaceBoundaryAreas() const { return surfaceBoundaryAreas; }
    uint32_t getDirichletRegionId(uint32_t nodeId) const;
    bool getRegionTemperatureC(uint32_t regionId, float& temperatureC) const;
    bool getRegionAmbientTemperatureC(uint32_t regionId, float& temperatureC) const;
    bool getRegionHeatFlux(uint32_t regionId, float& heatFlux) const;
    bool getRegionHeatTransferCoefficient(uint32_t regionId, float& coefficient) const;
    bool hasDirichletTemperature() const { return !dirichletNodeIds.empty(); }

private:
    static constexpr uint32_t NoBoundary = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t Adiabatic = 0u;
    static constexpr uint32_t DirichletTemperature = 1u;
    static constexpr uint32_t NeumannHeatFlux = 2u;
    static constexpr uint32_t RobinConvection = 3u;

    bool dirichletValuesAgree() const;
    uint32_t findStateIndex(uint32_t regionId) const;

    std::vector<Region> regions;
    std::unordered_map<uint32_t, uint32_t> stateIndexByRegionId;
    std::vector<heat::BoundaryState> states;
    std::vector<uint32_t> surfaceNodeIds;
    std::vector<float> surfaceBoundaryAreas;
    std::vector<uint8_t> surfaceNodeMask;
    std::vector<std::vector<uint32_t>> dirichletStateIndicesByNode;
    std::vector<std::vector<uint32_t>> dirichletStateIndicesBySurfacePoint;
    std::vector<uint32_t> dirichletRegionIdsByNode;
    std::vector<uint32_t> dirichletNodeIds;
    std::vector<heat::BoundaryNode> nodes;
    std::vector<heat::BoundaryContribution> contributions;
    std::vector<uint32_t> surfaceIndices;
    bool stateDirty = false;

    VkBuffer nodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize nodeBufferOffset = 0;
    VkBuffer contributionBuffer = VK_NULL_HANDLE;
    VkDeviceSize contributionBufferOffset = 0;
    VkBuffer surfaceIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceIndexBufferOffset = 0;
    VkBuffer stateBuffer = VK_NULL_HANDLE;
    VkDeviceSize stateBufferOffset = 0;
};
