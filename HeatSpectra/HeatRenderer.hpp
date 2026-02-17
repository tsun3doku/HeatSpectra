#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <vector>

class VulkanDevice;
class UniformBufferManager;
class Model;
class HeatReceiver;
class HeatSource;
class ResourceManager;

class HeatRenderer {
public:
    HeatRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HeatRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void cleanup();
    void updateDescriptors(
        ResourceManager& resourceManager,
        Model& heatModel,
        HeatSource& heatSource,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        uint32_t maxFramesInFlight,
        bool forceReallocate);
    void render(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        Model& heatModel,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers) const;

private:
    void createDescriptorPool(uint32_t maxFramesInFlight);
    void createDescriptorSetLayout();
    void createDescriptorSets(uint32_t maxFramesInFlight);
    void createPipeline(VkRenderPass renderPass);
    void drawModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, Model& model) const;
    bool updateDescriptorSetVector(
        const std::array<VkBufferView, 11>& bufferViews,
        uint32_t maxFramesInFlight,
        std::vector<VkDescriptorSet>& targetSets,
        bool forceReallocate);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::unordered_map<const HeatReceiver*, std::vector<VkDescriptorSet>> receiverDescriptorSets;

    bool initialized = false;
};
