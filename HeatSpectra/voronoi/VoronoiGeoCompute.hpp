#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanDevice;
class CommandPool;

class VoronoiGeoCompute {
public:
    struct PushConstants {
        uint32_t debugEnable = 0;
    };

    struct Bindings {
        VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
        VkDeviceSize voronoiNodeBufferOffset = 0;
        VkDeviceSize voronoiNodeBufferRange = 0;

        VkBuffer meshTriangleBuffer = VK_NULL_HANDLE;
        VkDeviceSize meshTriangleBufferOffset = 0;

        VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedPositionBufferOffset = 0;


        VkBuffer voxelGridParamsBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelGridParamsBufferOffset = 0;
        VkDeviceSize voxelGridParamsBufferRange = 0;

        VkBuffer voxelOccupancyBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelOccupancyBufferOffset = 0;


        VkBuffer voxelTrianglesListBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelTrianglesListBufferOffset = 0;

        VkBuffer voxelOffsetsBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelOffsetsBufferOffset = 0;

        VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
        VkDeviceSize neighborIndicesBufferOffset = 0;

        VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
        VkDeviceSize interfaceAreasBufferOffset = 0;

        VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
        VkDeviceSize interfaceNeighborIdsBufferOffset = 0;

        VkBuffer debugCellGeometryBuffer = VK_NULL_HANDLE;
        VkDeviceSize debugCellGeometryBufferOffset = 0;

        VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedFlagsBufferOffset = 0;

        VkBuffer voronoiDumpBuffer = VK_NULL_HANDLE;
        VkDeviceSize voronoiDumpBufferOffset = 0;
    };

    VoronoiGeoCompute(VulkanDevice& vulkanDevice, CommandPool& commandPool);
    ~VoronoiGeoCompute();

    void initialize(uint32_t nodeCount);
    void updateDescriptors(const Bindings& bindings);
    void dispatch(const PushConstants& pushConstants);

    void cleanupResources();
    void cleanup();

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSet();
    bool createPipeline();

    VulkanDevice& vulkanDevice;
    CommandPool& commandPool;

    uint32_t nodeCount = 0;
    bool initialized = false;

    Bindings currentBindings;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
