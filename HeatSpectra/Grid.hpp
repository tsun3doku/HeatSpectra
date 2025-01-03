#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class VulkanDevice;
class UniformBufferManager;

class Grid {
public:
    Grid() = default;

    void cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) const;
    void createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight); // Optional

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
   
    std::vector<VkImage> gridImages; // Optional
    std::vector<VkDeviceMemory> gridImageMemories; // Optional 
    std::vector<VkImageView> gridImageViews; // Optional
    VkImageCreateInfo gridImageInfo; // Optional
 
    VkDescriptorPool gridDescriptorPool;
    VkDescriptorSetLayout gridDescriptorSetLayout;
    std::vector<VkDescriptorSet> gridDescriptorSets;

    VkPipeline gridPipeline;
    VkPipelineLayout gridPipelineLayout;
};
