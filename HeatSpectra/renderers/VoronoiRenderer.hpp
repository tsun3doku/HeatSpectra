#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class Model;

class VoronoiRenderer {
public:
    VoronoiRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~VoronoiRenderer();

    // Setup the pipeline (call once or on resize)
    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    
    // Connect the data (call when buffers change)
    void updateDescriptors(uint32_t frameIndex,
        uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
        VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
        VkBufferView supportingHalfedgeView, VkBufferView supportingAngleView,
        VkBufferView halfedgeView, VkBufferView edgeView,
        VkBufferView triangleView, VkBufferView lengthView,
        VkBuffer candidateBuffer, VkDeviceSize candidateOffset);

    // The draw call (pass model matrix for transformations)
    void render(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
               VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
               uint32_t frameIndex, const glm::mat4& modelMatrix);

    void cleanup();

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    // Vulkan resources
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    bool initialized = false;
    
    // Stored for descriptor updates
    uint32_t currentVertexCount = 0;
    VkBuffer currentCandidateBuffer = VK_NULL_HANDLE;
};
