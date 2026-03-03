#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class LloydCompute {
public:
    struct Bindings {
        VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedPositionBufferOffset = 0;

        VkBuffer meshTriangleBuffer = VK_NULL_HANDLE;
        VkDeviceSize meshTriangleBufferOffset = 0;

        VkBuffer voxelTrianglesListBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelTrianglesListBufferOffset = 0;

        VkBuffer voxelOffsetsBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelOffsetsBufferOffset = 0;

        VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
        VkDeviceSize neighborIndicesBufferOffset = 0;

        VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedFlagsBufferOffset = 0;

        VkBuffer voxelGridParamsBuffer = VK_NULL_HANDLE;
        VkDeviceSize voxelGridParamsBufferOffset = 0;
        VkDeviceSize voxelGridParamsBufferRange = 0;
    };

    LloydCompute(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~LloydCompute();

    void initialize(uint32_t nodeCount);
    void updateDescriptors(const Bindings& bindings);

    void dispatch(int numIterations, float alpha, float maxStep);

    void cleanupResources();
    void cleanup();

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();

    void createBuffers(uint32_t nodeCount);
    void createAccumulatePipeline();
    void createUpdatePipeline();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;

    uint32_t nodeCount = 0;
    bool initialized = false;

    Bindings currentBindings;

    VkBuffer lloydAccumBuffer = VK_NULL_HANDLE;
    VkDeviceSize lloydAccumBufferOffset = 0;
    void* mappedLloydAccumData = nullptr;

    VkBuffer lloydParamsBuffer = VK_NULL_HANDLE;
    VkDeviceSize lloydParamsBufferOffset = 0;
    void* mappedLloydParamsData = nullptr;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout accumulatePipelineLayout = VK_NULL_HANDLE;
    VkPipeline accumulatePipeline = VK_NULL_HANDLE;

    VkPipelineLayout updatePipelineLayout = VK_NULL_HANDLE;
    VkPipeline updatePipeline = VK_NULL_HANDLE;
};
