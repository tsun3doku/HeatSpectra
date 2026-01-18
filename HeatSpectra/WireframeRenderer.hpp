#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class VulkanDevice;
class UniformBufferManager;
class Model;

class WireframeRenderer {
public:
    WireframeRenderer(VulkanDevice& device, VkDescriptorSetLayout geometryDescriptorSetLayout,
                     VkRenderPass renderPass, uint32_t subpass);
    ~WireframeRenderer();
    
    void bindPipeline(VkCommandBuffer cmdBuffer);
    void renderModel(VkCommandBuffer cmdBuffer, const Model& model,
                     VkDescriptorSet geometryDescriptorSet,
                     const glm::mat4& modelMatrix);
    
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    
    void cleanup();

private:
    void createPipeline(VkRenderPass renderPass, uint32_t subpass);

    VulkanDevice& vulkanDevice;
    VkDescriptorSetLayout geometryDescriptorSetLayout;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    bool initialized = false;
};
