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
    void updateDescriptors(Model& model, VkBuffer mappingBuffer, VkDeviceSize mappingOffset, 
                          uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
                          VkBuffer nodeBuffer, VkDeviceSize nodeOffset,
                          VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
                          uint32_t maxFrames);

    // The draw call (pass model matrix for transformations)
    void render(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
               VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
               uint32_t frameIndex, const glm::mat4& modelMatrix);

    void cleanup();

private:
    void createDescriptorSetLayout();
    void createDescriptorPool(uint32_t maxFramesInFlight);
    void createDescriptorSets(uint32_t maxFramesInFlight);
    void createPipeline(VkRenderPass renderPass);

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
    VkBuffer currentMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize currentMappingOffset = 0;
    uint32_t currentVertexCount = 0;
};
