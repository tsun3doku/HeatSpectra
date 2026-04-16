#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <unordered_map>
#include <vector>

#include "runtime/RuntimeProducts.hpp"

class VulkanDevice;
class UniformBufferManager;

class HeatReceiverRenderer {
public:
    struct ReceiverRenderBinding {
        ModelProduct model;
        std::array<VkBufferView, 11> bufferViews{};
    };

    HeatReceiverRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager);
    ~HeatReceiverRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void cleanup();
    void updateDescriptors(const std::vector<ReceiverRenderBinding>& receivers, uint32_t maxFramesInFlight, bool forceReallocate);
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<ReceiverRenderBinding>& receivers) const;

private:
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline(VkRenderPass renderPass);
    void drawModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, const ModelProduct& product) const;
    bool updateDescriptorSetVector(const std::array<VkBufferView, 11>& bufferViews, uint32_t maxFramesInFlight, std::vector<VkDescriptorSet>& targetSets, bool forceReallocate);

    VulkanDevice& vulkanDevice;
    UniformBufferManager& uniformBufferManager;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> receiverDescriptorSets;

    bool initialized = false;
};

