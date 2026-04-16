#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanDevice;
class UniformBufferManager;

class PointRenderer {
public:
    struct PointVertex {
        glm::vec3 position;
        glm::vec3 color;
    };

    PointRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~PointRenderer();
    
    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void render(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        VkBuffer vertexBuffer,
        VkDeviceSize vertexBufferOffset,
        uint32_t pointCount,
        const glm::mat4& modelMatrix,
        VkExtent2D extent);
    
    void setPointSize(float size) { pointSize = size; }
    void setVisible(bool vis) { visible = vis; }
    bool isVisible() const { return visible; }
    
    void cleanup();

private:
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpass);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;
    
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    float pointSize = 0.00075f; 
    bool visible = true;
    bool initialized = false;
};
