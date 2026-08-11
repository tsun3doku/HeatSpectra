#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>

class VulkanDevice;
class UniformBufferManager;

class WireframeRenderer {
public:
    WireframeRenderer(VulkanDevice& device, VkDescriptorSetLayout geometryDescriptorSetLayout,
                     VkRenderPass renderPass, uint32_t subpass);
    ~WireframeRenderer();
    
    void bindPipeline(VkCommandBuffer cmdBuffer);
    void renderModel(
        VkCommandBuffer cmdBuffer,
        VkDescriptorSet geometryDescriptorSet,
        const glm::mat4& modelMatrix,
        VkBuffer vertexBuffer,
        VkDeviceSize vertexBufferOffset,
        VkBuffer indexBuffer,
        VkDeviceSize indexBufferOffset,
        uint32_t indexCount);
    
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
