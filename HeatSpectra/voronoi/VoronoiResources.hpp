#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

class VoronoiResources {
public:
    VoronoiResources() = default;
    ~VoronoiResources() = default;

    uint32_t voronoiNodeCount = 0;

    VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNodeBufferOffset = 0;
    void* mappedVoronoiNodeData = nullptr;

    VkBuffer voronoiNeighborBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNeighborBufferOffset = 0;

    VkBuffer debugCellGeometryBuffer = VK_NULL_HANDLE;
    VkDeviceSize debugCellGeometryBufferOffset = 0;
    void* mappedDebugCellGeometryData = nullptr;

    VkBuffer voronoiDumpBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiDumpBufferOffset = 0;
    void* mappedVoronoiDumpData = nullptr;

    VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize neighborIndicesBufferOffset = 0;

    VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceAreasBufferOffset = 0;
    void* mappedInterfaceAreasData = nullptr;

    VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceNeighborIdsBufferOffset = 0;
    void* mappedInterfaceNeighborIdsData = nullptr;

    VkBuffer meshTriangleBuffer = VK_NULL_HANDLE;
    VkDeviceSize meshTriangleBufferOffset = 0;

    VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedPositionBufferOffset = 0;
    void* mappedSeedPositionData = nullptr;

    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset = 0;
    void* mappedSeedFlagsData = nullptr;

    VkBuffer occupancyPointBuffer = VK_NULL_HANDLE;
    VkDeviceSize occupancyPointBufferOffset = 0;
    uint32_t occupancyPointCount = 0;

    VkBuffer voxelGridParamsBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelGridParamsBufferOffset = 0;

    VkBuffer voxelOccupancyBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelOccupancyBufferOffset = 0;

    VkBuffer voxelTrianglesListBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelTrianglesListBufferOffset = 0;

    VkBuffer voxelOffsetsBuffer = VK_NULL_HANDLE;
    VkDeviceSize voxelOffsetsBufferOffset = 0;
};
