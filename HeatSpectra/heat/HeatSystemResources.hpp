#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class HeatSystemResources {
public:
    HeatSystemResources() = default;
    ~HeatSystemResources() = default;

    uint32_t voronoiNodeCount = 0;
    const void* mappedVoronoiNodeData = nullptr;
    VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNodeBufferOffset = 0;

    VkBuffer gmlsInterfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsInterfaceBufferOffset = 0;

    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset = 0;

    VkDescriptorPool voronoiDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> voronoiDescriptorSets;
    std::vector<VkDescriptorSet> voronoiDescriptorSetsB;

    VkPipelineLayout voronoiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline voronoiPipeline = VK_NULL_HANDLE;
    VkBuffer voronoiMaterialNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMaterialNodeBufferOffset = 0;
    void* mappedVoronoiMaterialNodeData = nullptr;

    VkDescriptorPool surfaceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout surfaceDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout surfacePipelineLayout = VK_NULL_HANDLE;
    VkPipeline surfacePipeline = VK_NULL_HANDLE;

    VkDescriptorPool contactDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout contactDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout contactPipelineLayout = VK_NULL_HANDLE;
    VkPipeline contactPipeline = VK_NULL_HANDLE;

    VkBuffer contactConductanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactConductanceBufferOffset = 0;
    uint32_t contactConductanceNodeCount = 0;
    bool hasContact = false;
};
