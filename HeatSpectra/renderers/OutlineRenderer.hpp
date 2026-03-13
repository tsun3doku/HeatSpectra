#pragma once

#include <vulkan/vulkan.h>

#include <vector>

class VulkanDevice;
class ModelSelection;

class OutlineRenderer {
public:
    OutlineRenderer(VulkanDevice& device, VkRenderPass renderPass, uint32_t subpassIndex, uint32_t maxFramesInFlight);
    ~OutlineRenderer();

    void updateDescriptors(const std::vector<VkImageView>& depthViews, const std::vector<VkImageView>& stencilViews);
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ModelSelection& modelSelection);
    void cleanup();

private:
    bool createDepthSampler();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline(VkRenderPass renderPass, uint32_t subpassIndex);

    VulkanDevice& vulkanDevice;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    VkSampler depthSampler = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};
