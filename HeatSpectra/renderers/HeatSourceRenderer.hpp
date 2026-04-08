#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

#include "runtime/HeatOverlayData.hpp"
#include "runtime/RuntimeProducts.hpp"

class VulkanDevice;
class UniformBufferManager;
class ModelRegistry;
struct UniformBufferObject;

class HeatSourceRenderer {
public:
    HeatSourceRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HeatSourceRenderer();

    void initialize(VkRenderPass renderPass);
    void cleanup();

    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<HeatOverlayData>& sources, ModelRegistry& resourceManager) const;

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

