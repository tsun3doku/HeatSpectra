#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class VulkanDevice;
class UniformBufferManager;

class Grid {
public:
    Grid() = default;

    void createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight);

    void createGridDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGridDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGridDescriptorSets(const VulkanDevice& vulkanDevice, const UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createGridPipeline(const VulkanDevice& vulkanDevice, VkRenderPass renderPass);

    uint32_t vertexCount = 6;

    //Getters
    VkDescriptorPool getGridDescriptorPool() const {
        return gridDescriptorPool;
    }
    VkDescriptorSetLayout getGridDescriptorSetLayout() const {
        return gridDescriptorSetLayout;
    }
    std::vector<VkDescriptorSet> getGridDescriptorSets() const {
        return gridDescriptorSets;
    }

    VkPipeline getGridPipeline() const {
        return gridPipeline;
    }
    VkPipelineLayout getGridPipelineLayout() const {
        return gridPipelineLayout;
    }

    std::vector<VkImage> getGridImages() const {
        return gridImages;
    }

    std::vector<VkDeviceMemory> getGridImageMemories() const {
        return gridImageMemories;
    }

    std::vector<VkImageView> getGridImageViews() const {
        return gridImageViews;
    }

private:
    const VulkanDevice* vulkanDevice;
    const UniformBufferManager* uniformBufferManager;
   
    uint32_t currentFrame = 0;

    std::vector<VkImage> gridImages;
    std::vector<VkDeviceMemory> gridImageMemories;
    std::vector<VkImageView> gridImageViews;
    VkImageCreateInfo gridImageInfo;
 
    VkDescriptorPool gridDescriptorPool;
    VkDescriptorSetLayout gridDescriptorSetLayout;
    std::vector<VkDescriptorSet> gridDescriptorSets;

    VkPipeline gridPipeline;
    VkPipelineLayout gridPipelineLayout;
};
