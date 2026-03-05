#pragma once

#include <vulkan/vulkan.h>

#include <vector>

class HeatSystemResources {
public:
    HeatSystemResources() = default;
    ~HeatSystemResources() = default;

    uint32_t voronoiNodeCount = 0;

    VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNodeBufferOffset_ = 0;
    void* mappedVoronoiNodeData = nullptr;

    VkBuffer voronoiNeighborBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNeighborBufferOffset_ = 0;

    VkDescriptorPool voronoiDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> voronoiDescriptorSets;
    std::vector<VkDescriptorSet> voronoiDescriptorSetsB;

    VkPipelineLayout voronoiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline voronoiPipeline = VK_NULL_HANDLE;

    VkBuffer debugCellGeometryBuffer = VK_NULL_HANDLE;
    VkDeviceSize debugCellGeometryBufferOffset_ = 0;
    void* mappedDebugCellGeometryData = nullptr;

    VkBuffer voronoiDumpBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiDumpBufferOffset_ = 0;
    void* mappedVoronoiDumpData = nullptr;

    VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize neighborIndicesBufferOffset_ = 0;

    VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceAreasBufferOffset_ = 0;
    void* mappedInterfaceAreasData = nullptr;

    VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceNeighborIdsBufferOffset_ = 0;
    void* mappedInterfaceNeighborIdsData = nullptr;

    VkBuffer meshTriangleBuffer = VK_NULL_HANDLE;
    VkDeviceSize meshTriangleBufferOffset_ = 0;

    VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedPositionBufferOffset_ = 0;
    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset_ = 0;
    void* mappedSeedPositionData = nullptr;

    VkBuffer voxelGridParamsBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelGridParamsBufferOffset_ = 0;

    VkBuffer voxelOccupancyBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelOccupancyBufferOffset_ = 0;

    VkBuffer voxelTrianglesListBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelTrianglesListBufferOffset_ = 0;

    VkBuffer voxelOffsetsBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelOffsetsBufferOffset_ = 0;

    std::vector<VkCommandBuffer> computeCommandBuffers;

    VkBuffer tempBufferA = VK_NULL_HANDLE;
    VkDeviceSize tempBufferAOffset_ = 0;
    void* mappedTempBufferA = nullptr;

    VkBuffer tempBufferB = VK_NULL_HANDLE;
    VkDeviceSize tempBufferBOffset_ = 0;
    void* mappedTempBufferB = nullptr;

    VkBuffer injectionKBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKBufferOffset_ = 0;
    void* mappedInjectionKBuffer = nullptr;

    VkBuffer injectionKTBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKTBufferOffset_ = 0;
    void* mappedInjectionKTBuffer = nullptr;

    VkBuffer timeBuffer = VK_NULL_HANDLE;
    VkDeviceSize timeBufferOffset_ = 0;
    void* mappedTimeData = nullptr;

    VkDescriptorPool surfaceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout surfaceDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout surfacePipelineLayout = VK_NULL_HANDLE;
    VkPipeline surfacePipeline = VK_NULL_HANDLE;

    VkDescriptorPool contactDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout contactDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout contactPipelineLayout = VK_NULL_HANDLE;
    VkPipeline contactPipeline = VK_NULL_HANDLE;
};

