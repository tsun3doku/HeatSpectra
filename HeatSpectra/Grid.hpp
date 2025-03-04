#pragma once

class UniformBufferManager;
class ResourceManager;
class VulkanDevice;

class Grid {
public:
    Grid(VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, VkRenderPass renderPass);
    ~Grid();

    void cleanup(VulkanDevice& vulkanDevice) const;

    void createGridDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGridDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGridDescriptorSets(const VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createGridPipeline(const VulkanDevice& vulkanDevice, VkRenderPass renderPass);

    uint32_t vertexCount = 6;

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
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
 
    VkDescriptorPool gridDescriptorPool;
    VkDescriptorSetLayout gridDescriptorSetLayout;
    std::vector<VkDescriptorSet> gridDescriptorSets;

    VkPipeline gridPipeline;
    VkPipelineLayout gridPipelineLayout;
};
