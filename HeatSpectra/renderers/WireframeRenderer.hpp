#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>

class VulkanDevice;
class UniformBufferManager;
class Model;

class WireframeRenderer {
public:
    struct DrawItem {
        const Model* model = nullptr;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
    };

    WireframeRenderer(VulkanDevice& device, VkDescriptorSetLayout geometryDescriptorSetLayout,
                     VkRenderPass renderPass, uint32_t subpass);
    ~WireframeRenderer();
    
    void bindPipeline(VkCommandBuffer cmdBuffer);
    void renderModels(VkCommandBuffer cmdBuffer, VkDescriptorSet geometryDescriptorSet, const DrawItem* items, uint32_t itemCount);
    void renderModel(VkCommandBuffer cmdBuffer, const Model& model, VkDescriptorSet geometryDescriptorSet, const glm::mat4& modelMatrix);
    
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    
    void cleanup();

private:
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);

    VulkanDevice& vulkanDevice;
    VkDescriptorSetLayout geometryDescriptorSetLayout;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    bool initialized = false;
};
