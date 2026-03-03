#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class Model;
class HashGrid;

class HashGridRenderer {
public:
    HashGridRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HashGridRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);

    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);

    bool allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, HashGrid* hashGrid, uint32_t maxFramesInFlight);
    
    void render(VkCommandBuffer cmdBuffer, Model* model, HashGrid* hashGrid, uint32_t frameIndex, const glm::mat4& modelMatrix, const glm::vec3& color);
    
    void cleanup();

private:

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelDescriptorSets;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    bool initialized = false;
};
