#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class HeatReceiver;

class VoronoiRenderer {
public:
    VoronoiRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~VoronoiRenderer();

    // Setup the pipeline (call once or on resize)
    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    
    // Connect receiver data (call when buffers change)
    void updateDescriptors(const HeatReceiver* receiverKey, uint32_t frameIndex,
        uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
        VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
        VkBufferView supportingHalfedgeView, VkBufferView supportingAngleView,
        VkBufferView halfedgeView, VkBufferView edgeView,
        VkBufferView triangleView, VkBufferView lengthView,
        VkBuffer candidateBuffer, VkDeviceSize candidateOffset);

    // Draw a specific receiver with its descriptor set.
    void render(VkCommandBuffer cmd, const HeatReceiver* receiverKey,
               VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
               VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
               uint32_t frameIndex, const glm::mat4& modelMatrix);

    void cleanup();

private:
    void createDescriptorSetLayout();
    void createDescriptorPool(uint32_t maxFramesInFlight);
    void createPipeline(VkRenderPass renderPass);
    bool ensureReceiverDescriptorSets(const HeatReceiver* receiverKey);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    // Vulkan resources
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::unordered_map<const HeatReceiver*, std::vector<VkDescriptorSet>> receiverDescriptorSets;
    std::unordered_map<const HeatReceiver*, VkBuffer> receiverCandidateBuffers;
    
    bool initialized = false;
    uint32_t maxFramesInFlight = 0;
};
