#include "HeatSystemSurfaceStage.hpp"

#include "heat/HeatModelRuntime.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "scene/Model.hpp"

#include "util/Structs.hpp"
#include "util/file_utils.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <iostream>
#include <vector>

HeatSystemSurfaceStage::HeatSystemSurfaceStage(VulkanDevice& device)
    : vulkanDevice(device) {
}

HeatSystemSurfaceStage::~HeatSystemSurfaceStage() {
    VkDevice device = vulkanDevice.getDevice();
    if (gradientPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, gradientPipeline, nullptr);
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (gradientPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, gradientPipelineLayout, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (gradientDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, gradientDescriptorSetLayout, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
}

bool HeatSystemSurfaceStage::createDescriptorPool(uint32_t numModels) {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    const uint32_t effectiveModels = (numModels > 0) ? numModels : 1;
    const uint32_t totalSets = effectiveModels * 8;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = totalSets * 6;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr,
        &descriptorPool) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool HeatSystemSurfaceStage::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> tempBindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo tempLayoutInfo{};
    tempLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tempLayoutInfo.flags = 0;
    tempLayoutInfo.bindingCount = static_cast<uint32_t>(tempBindings.size());
    tempLayoutInfo.pBindings = tempBindings.data();
    tempLayoutInfo.pNext = nullptr;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &tempLayoutInfo, nullptr,
        &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface temperature descriptor set layout" << std::endl;
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> gradientBindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo gradientLayoutInfo{};
    gradientLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    gradientLayoutInfo.flags = 0;
    gradientLayoutInfo.bindingCount = static_cast<uint32_t>(gradientBindings.size());
    gradientLayoutInfo.pBindings = gradientBindings.data();
    gradientLayoutInfo.pNext = nullptr;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &gradientLayoutInfo, nullptr,
        &gradientDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface gradient descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool HeatSystemSurfaceStage::createPipeline() {
    // Temperature pipeline 
    auto tempShaderCode = readFile("shaders/heat_surface_temp_comp.spv");
    VkShaderModule tempShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, tempShaderCode, tempShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface temperature compute shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo tempShaderStageInfo{};
    tempShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tempShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    tempShaderStageInfo.module = tempShaderModule;
    tempShaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(heat::HeatModelPushConstant);

    VkPipelineLayoutCreateInfo tempPipelineLayoutInfo{};
    tempPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tempPipelineLayoutInfo.setLayoutCount = 1;
    tempPipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    tempPipelineLayoutInfo.pushConstantRangeCount = 1;
    tempPipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &tempPipelineLayoutInfo, nullptr,
        &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), tempShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface temperature pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo tempPipelineInfo{};
    tempPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    tempPipelineInfo.stage = tempShaderStageInfo;
    tempPipelineInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &tempPipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), tempShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface temperature compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), tempShaderModule, nullptr);

    // Gradient pipeline 
    auto gradientShaderCode = readFile("shaders/heat_surface_gradient_comp.spv");
    VkShaderModule gradientShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, gradientShaderCode, gradientShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface gradient compute shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo gradientShaderStageInfo{};
    gradientShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    gradientShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    gradientShaderStageInfo.module = gradientShaderModule;
    gradientShaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo gradientPipelineLayoutInfo{};
    gradientPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    gradientPipelineLayoutInfo.setLayoutCount = 1;
    gradientPipelineLayoutInfo.pSetLayouts = &gradientDescriptorSetLayout;
    gradientPipelineLayoutInfo.pushConstantRangeCount = 1;
    gradientPipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &gradientPipelineLayoutInfo, nullptr,
        &gradientPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), gradientShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface gradient pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo gradientPipelineInfo{};
    gradientPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    gradientPipelineInfo.stage = gradientShaderStageInfo;
    gradientPipelineInfo.layout = gradientPipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &gradientPipelineInfo, nullptr, &gradientPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), gradientPipelineLayout, nullptr);
        gradientPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), gradientShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface gradient compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), gradientShaderModule, nullptr);
    return true;
}

void HeatSystemSurfaceStage::dispatchSurfaceTemperatureUpdates(
    VkCommandBuffer commandBuffer,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    bool replayFromHistory,
    bool finalWritesBufferB,
    uint32_t currentFrame) const {
    dispatchSurfacePass(commandBuffer, pipeline, pipelineLayout, activeModels, replayFromHistory, finalWritesBufferB, false, currentFrame);
}

void HeatSystemSurfaceStage::dispatchSurfaceGradientUpdates(
    VkCommandBuffer commandBuffer,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    bool replayFromHistory,
    bool finalWritesBufferB,
    uint32_t currentFrame) const {
    dispatchSurfacePass(commandBuffer, gradientPipeline, gradientPipelineLayout, activeModels, replayFromHistory, finalWritesBufferB, true, currentFrame);
}


void HeatSystemSurfaceStage::dispatchSurfacePass(
    VkCommandBuffer commandBuffer,
    VkPipeline pipeline,
    VkPipelineLayout layout,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& activeModels,
    bool replayFromHistory,
    bool finalWritesBufferB,
    bool isGradientPass,
    uint32_t currentFrame) const {
    if (pipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    heat::HeatModelPushConstant surfacePushConstant{};
    surfacePushConstant.elementCount = 0;
    const bool useB = (currentFrame % 2) == 1;

    for (const auto& [runtimeModelId, heatModel] : activeModels) {
        if (!heatModel || heatModel->getSurfaceVertexCount() == 0) {
            continue;
        }

        VkDescriptorSet set = VK_NULL_HANDLE;
        if (replayFromHistory) {
            set = isGradientPass
                ? (useB ? heatModel->getSurfaceGradientHistorySetB() : heatModel->getSurfaceGradientHistorySetA())
                : (useB ? heatModel->getSurfaceHistoryComputeSetB() : heatModel->getSurfaceHistoryComputeSetA());
        } else if (isGradientPass) {
            set = finalWritesBufferB ? heatModel->getSurfaceGradientComputeSetB() : heatModel->getSurfaceGradientComputeSetA();
        } else {
            set = finalWritesBufferB ? heatModel->getSurfaceComputeSetB() : heatModel->getSurfaceComputeSetA();
        }

        if (set != VK_NULL_HANDLE) {
            const uint32_t vertexCount = static_cast<uint32_t>(heatModel->getSurfaceVertexCount());
            const uint32_t workGroupCount = (vertexCount + 255) / 256;

            surfacePushConstant.elementCount = vertexCount;
            surfacePushConstant.metersPerUnit = heatModel->getMetersPerWorldUnit();
            vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(heat::HeatModelPushConstant), &surfacePushConstant);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
            vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
        }
    }
}
