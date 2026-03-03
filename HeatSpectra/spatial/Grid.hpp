#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class UniformBufferManager;
class VulkanDevice;
class GridLabel;
class CommandPool;

class GridRenderer {
public:
    GridRenderer(VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager,
                 uint32_t maxFramesInFlight, VkRenderPass renderPass, CommandPool& commandPool);
    ~GridRenderer();

    void cleanup() const;
    void updateLabels(const glm::vec3& gridSize);
    void renderLabels(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void createGridDescriptorPool(uint32_t maxFramesInFlight);
    void createGridDescriptorSetLayout();
    void createGridDescriptorSets(uint32_t maxFramesInFlight);

    void createGridPipeline(VkRenderPass renderPass);

    uint32_t vertexCount = 30; 

    //Getters
    const VkDescriptorPool& getGridDescriptorPool() const {
        return gridDescriptorPool;
    }
    const VkDescriptorSetLayout& getGridDescriptorSetLayout() const {
        return gridDescriptorSetLayout;
    }
    const std::vector<VkDescriptorSet>& getGridDescriptorSets() const {
        return gridDescriptorSets;
    }

    const VkPipeline& getGridPipeline() const {
        return gridPipeline;
    }
    const VkPipelineLayout& getGridPipelineLayout() const {
        return gridPipelineLayout;
    }

private:
    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;
 
    VkDescriptorPool gridDescriptorPool;
    VkDescriptorSetLayout gridDescriptorSetLayout;
    std::vector<VkDescriptorSet> gridDescriptorSets;

    VkPipeline gridPipeline;
    VkPipelineLayout gridPipelineLayout;
    
    std::unique_ptr<GridLabel> gridLabel;
};
