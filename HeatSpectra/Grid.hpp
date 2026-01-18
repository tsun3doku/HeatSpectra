#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class UniformBufferManager;
class ResourceManager;
class VulkanDevice;
class GridLabel;
class MemoryAllocator;

class Grid {
public:
    public:
    Grid(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,
         UniformBufferManager& uniformBufferManager,
         uint32_t maxFramesInFlight, VkRenderPass renderPass, CommandPool& commandPool);
    ~Grid();

    void cleanup(VulkanDevice& vulkanDevice) const;
    void updateLabels(const glm::vec3& gridSize);
    void renderLabels(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void createGridDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGridDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGridDescriptorSets(const VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createGridPipeline(const VulkanDevice& vulkanDevice, VkRenderPass renderPass);

    uint32_t vertexCount = 30;  // 5 planes * 6 vertices (2 triangles) each

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
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
 
    VkDescriptorPool gridDescriptorPool;
    VkDescriptorSetLayout gridDescriptorSetLayout;
    std::vector<VkDescriptorSet> gridDescriptorSets;

    VkPipeline gridPipeline;
    VkPipelineLayout gridPipelineLayout;
    
    std::unique_ptr<GridLabel> gridLabel;
};
