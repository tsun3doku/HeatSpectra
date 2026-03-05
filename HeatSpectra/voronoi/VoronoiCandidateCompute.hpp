#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanDevice;
class CommandPool;

class VoronoiCandidateCompute {
public:
    struct Bindings {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferOffset = 0;

        VkBuffer faceIndexBuffer = VK_NULL_HANDLE;
        VkDeviceSize faceIndexBufferOffset = 0;

        VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedPositionBufferOffset = 0;

        VkBuffer candidateBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateBufferOffset = 0;
    };

    VoronoiCandidateCompute(VulkanDevice& device, CommandPool& commandPool);
    ~VoronoiCandidateCompute();

    void initialize();
    void updateDescriptors(const Bindings& bindings);
    void dispatch(uint32_t faceCount, uint32_t seedCount, uint32_t seedOffset);

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
    Bindings currentBindings{};

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
