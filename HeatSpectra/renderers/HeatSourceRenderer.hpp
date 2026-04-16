#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "runtime/RuntimeProducts.hpp"

class VulkanDevice;
class UniformBufferManager;
struct UniformBufferObject;

class HeatSourceRenderer {
public:
    struct SourceRenderBinding {
        ModelProduct model;
        float sourceTemperature = 0.0f;
    };

    HeatSourceRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HeatSourceRenderer();

    void initialize(VkRenderPass renderPass);
    void cleanup();

    void render(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        const std::vector<SourceRenderBinding>& sources) const;

private:
    bool createPipeline(VkRenderPass renderPass);
    void drawModel(
        VkCommandBuffer commandBuffer,
        const ModelProduct& product,
        float sourceTemperature,
        const UniformBufferObject& ubo) const;

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    bool initialized = false;
};

