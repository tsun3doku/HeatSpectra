#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class HashGrid;
class ModelRegistry;

class HashGridRenderer {
public:
    HashGridRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HashGridRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);

    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);

    bool allocateDescriptorSetsForModel(uint32_t runtimeModelId, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(uint32_t runtimeModelId, HashGrid* hashGrid, uint32_t maxFramesInFlight);
    
    void render(
        VkCommandBuffer cmdBuffer,
        uint32_t runtimeModelId,
        HashGrid* hashGrid,
        uint32_t frameIndex,
        ModelRegistry& resourceManager,
        const glm::vec3& color);
    
    void cleanup();

private:

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> perModelDescriptorSets;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    bool initialized = false;
};

