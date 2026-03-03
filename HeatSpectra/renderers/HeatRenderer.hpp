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
class Remesher;

class HeatRenderer {
public:
    struct SourceRenderBinding {
        Model* model = nullptr;
        HeatSource* heatSource = nullptr;
    };

    HeatRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HeatRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void cleanup();
    void updateDescriptors(
        Remesher& remesher,
        const std::vector<SourceRenderBinding>& sources,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        uint32_t maxFramesInFlight,
        bool forceReallocate);
    void render(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        const std::vector<SourceRenderBinding>& sources,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers) const;

private:
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline(VkRenderPass renderPass);
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

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::unordered_map<const Model*, std::vector<VkDescriptorSet>> sourceDescriptorSets;
    std::unordered_map<const HeatReceiver*, std::vector<VkDescriptorSet>> receiverDescriptorSets;

    bool initialized = false;
};
