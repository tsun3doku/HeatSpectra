#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanDevice;
class CommandPool;

class VoronoiCandidateCompute {
public:
    VoronoiCandidateCompute(VulkanDevice& device, CommandPool& commandPool);
    ~VoronoiCandidateCompute();

    void initialize();
    void updateDescriptors(
        VkBuffer vertexBuffer, VkDeviceSize vertexBufferOffset,
        VkBuffer faceIndexBuffer, VkDeviceSize faceIndexBufferOffset,
        VkBuffer seedPositionBuffer, VkDeviceSize seedPositionBufferOffset,
        VkBuffer candidateBuffer, VkDeviceSize candidateBufferOffset);
    void dispatch(uint32_t faceCount, uint32_t seedCount);

    void cleanupResources();
    void cleanup();

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSet();
    bool createPipeline();

    VulkanDevice& vulkanDevice;
    CommandPool& commandPool;

    bool initialized = false;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
