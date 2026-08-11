#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

#include "HeatSystemPresets.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "util/Structs.hpp"

class PointRenderer;
class HeatPlaybackRuntime;
class VulkanDevice;

class HeatSystemDiffusionStage {
public:
    explicit HeatSystemDiffusionStage(VulkanDevice& device);
    ~HeatSystemDiffusionStage();

    void dispatchDiffusionSubstep(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet descriptorSet,
        const heat::HeatModelPushConstant& pushConstant,
        uint32_t workGroupCount) const;

    bool createDescriptorPool(uint32_t numModels);
    bool createDescriptorSetLayout();
    bool createPipeline();

    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

private:
    VulkanDevice& vulkanDevice;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
