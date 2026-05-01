#include "HeatSystemSurfaceStage.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "scene/Model.hpp"

#include "util/Structs.hpp"
#include "util/file_utils.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <iostream>
#include <vector>

HeatSystemSurfaceStage::HeatSystemSurfaceStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemSurfaceStage::refreshSurfaceDescriptors(uint32_t nodeCount) {
    (void)nodeCount;
}

bool HeatSystemSurfaceStage::createDescriptorPool(uint32_t maxFramesInFlight) {
    (void)maxFramesInFlight;
    const uint32_t maxHeatModels = 10;
    const uint32_t totalSets = (maxHeatModels * 2);

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = totalSets * 5;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(context.vulkanDevice.getDevice(), &poolInfo, nullptr,
        &context.resources.surfaceDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemSurfaceStage::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

    bindingFlags.bindingCount = static_cast<uint32_t>(flags.size());
    bindingFlags.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.pNext = &bindingFlags;

    if (vkCreateDescriptorSetLayout(context.vulkanDevice.getDevice(), &layoutInfo, nullptr,
        &context.resources.surfaceDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemSurfaceStage::createPipeline() {
    auto computeShaderCode = readFile("shaders/heat_surface_comp.spv");
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(context.vulkanDevice, computeShaderCode, computeShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create surface compute shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(heat::SourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.resources.surfaceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &context.resources.surfacePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = context.resources.surfacePipelineLayout;

    if (vkCreateComputePipelines(context.vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &context.resources.surfacePipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(context.vulkanDevice.getDevice(), context.resources.surfacePipelineLayout, nullptr);
        context.resources.surfacePipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create surface compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
    return true;
}

void HeatSystemSurfaceStage::dispatchSurfaceTemperatureUpdates(
    VkCommandBuffer commandBuffer,
    uint32_t nodeCount,
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    bool finalWritesBufferB) const {
    if (nodeCount == 0 ||
        context.resources.surfacePipeline == VK_NULL_HANDLE ||
        context.resources.surfacePipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.resources.surfacePipeline);

    heat::SourcePushConstant surfacePushConstant{};
    surfacePushConstant.substepIndex = 0;

    for (const auto& receiver : receivers) {
        if (!receiver || receiver->getIntrinsicVertexCount() == 0) {
            continue;
        }

        const VkDescriptorSet surfaceSet = finalWritesBufferB
            ? receiver->getSurfaceComputeSetB()
            : receiver->getSurfaceComputeSetA();
        if (surfaceSet == VK_NULL_HANDLE) {
            continue;
        }

        const uint32_t workGroupSize = 256;
        const uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
        const uint32_t workGroupCount = (vertexCount + workGroupSize - 1) / workGroupSize;

        vkCmdPushConstants(
            commandBuffer,
            context.resources.surfacePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(heat::SourcePushConstant),
            &surfacePushConstant);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            context.resources.surfacePipelineLayout,
            0,
            1,
            &surfaceSet,
            0,
            nullptr);
        vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
    }
}
