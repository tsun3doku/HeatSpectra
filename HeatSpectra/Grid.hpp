#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class UniformBufferManager;
class ResourceManager;
class VulkanDevice;

class Grid {
public:
    Grid(VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight, VkRenderPass renderPass);
    ~Grid();

    void cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) const;
    void createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight); // Optional

    void createGridDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGridDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGridDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight);

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
    VulkanDevice* vulkanDevice;
    //UniformBufferManager* uniformBufferManager;
    ResourceManager& resourceManager;
   
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
