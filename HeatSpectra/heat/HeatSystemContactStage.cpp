#include "HeatSystemContactStage.hpp"

#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "HeatContactParams.hpp"
#include "HeatSourceRuntime.hpp"
#include "util/file_utils.h"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <cstring>
#include <iostream>
HeatSystemContactStage::HeatSystemContactStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

bool HeatSystemContactStage::ensureParamsBuffer(HeatContactRuntime::CouplingState& coupling) {
    if (coupling.paramsBuffer == VK_NULL_HANDLE) {
        const auto bufferResult = context.memoryAllocator.allocate(
            sizeof(HeatContactParams),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            64);
        coupling.paramsBuffer = bufferResult.first;
        coupling.paramsBufferOffset = bufferResult.second;
        if (coupling.paramsBuffer == VK_NULL_HANDLE) {
            return false;
        }
    }

    void* mappedData = context.memoryAllocator.getMappedPointer(coupling.paramsBuffer, coupling.paramsBufferOffset);
    if (!mappedData) {
        return false;
    }

    std::memcpy(mappedData, &coupling.params, sizeof(HeatContactParams));
    return true;
}

const HeatSystemRuntime::SourceBinding* HeatSystemContactStage::findSourceBindingByRuntimeModelId(
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    uint32_t runtimeModelId) const {
    for (const HeatSystemRuntime::SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.runtimeModelId == runtimeModelId && sourceBinding.heatSource) {
            return &sourceBinding;
        }
    }
    return nullptr;
}

bool HeatSystemContactStage::createDescriptorPool(uint32_t maxFramesInFlight) {
    (void)maxFramesInFlight;
    const uint32_t maxReceivers = 24;
    const uint32_t maxHeatSources = 16;
    const uint32_t maxCouplings =
        (maxHeatSources * maxReceivers) +
        (maxReceivers * (maxReceivers - 1));
    const uint32_t totalSets = maxCouplings * 2;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = totalSets * 8;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(context.vulkanDevice.getDevice(), &poolInfo, nullptr,
        &context.resources.contactDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create contact descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemContactStage::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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
        &context.resources.contactDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create contact descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemContactStage::createPipeline() {
    auto computeShaderCode = readFile("shaders/heat_contact_comp.spv");
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(context.vulkanDevice, computeShaderCode, computeShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create contact compute shader module" << std::endl;
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
    pushConstantRange.size = sizeof(ContactPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.resources.contactDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &context.resources.contactPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create contact pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = context.resources.contactPipelineLayout;

    if (vkCreateComputePipelines(context.vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &context.resources.contactPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(context.vulkanDevice.getDevice(), context.resources.contactPipelineLayout, nullptr);
        context.resources.contactPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create contact compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
    return true;
}

void HeatSystemContactStage::updateCouplingDescriptors(
    HeatContactRuntime::CouplingState& coupling,
    const HeatSystemSimRuntime& simRuntime) {
    coupling.contactDescriptorsReady = false;
    if (coupling.contactSampleBuffer == VK_NULL_HANDLE ||
        coupling.contactSampleCount == 0 ||
        coupling.contactCellMapBuffer == VK_NULL_HANDLE ||
        coupling.contactCellMapCount == 0 ||
        coupling.contactCellRangeBuffer == VK_NULL_HANDLE ||
        coupling.contactCellRangeCount == 0 ||
        !ensureParamsBuffer(coupling)) {
        return;
    }

    if (coupling.contactComputeSetA == VK_NULL_HANDLE || coupling.contactComputeSetB == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayout, 2> layouts = {
            context.resources.contactDescriptorSetLayout,
            context.resources.contactDescriptorSetLayout
        };

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context.resources.contactDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        std::array<VkDescriptorSet, 2> sets = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        if (vkAllocateDescriptorSets(context.vulkanDevice.getDevice(), &allocInfo, sets.data()) != VK_SUCCESS) {
            coupling.contactComputeSetA = VK_NULL_HANDLE;
            coupling.contactComputeSetB = VK_NULL_HANDLE;
            return;
        }

        coupling.contactComputeSetA = sets[0];
        coupling.contactComputeSetB = sets[1];
    }

    auto updateSet = [this, &coupling, &simRuntime](VkDescriptorSet descriptorSet, VkBuffer tempReadBuffer, VkDeviceSize tempReadOffset) {
        const VkBuffer emitterTriangleIndexBuffer =
            coupling.emitterTriangleIndexBuffer != VK_NULL_HANDLE
            ? coupling.emitterTriangleIndexBuffer
            : simRuntime.getInjectionKBuffer();
        const VkDeviceSize emitterTriangleIndexBufferOffset =
            coupling.emitterTriangleIndexBuffer != VK_NULL_HANDLE
            ? coupling.emitterTriangleIndexBufferOffset
            : simRuntime.getInjectionKBufferOffset();
        const VkBuffer emitterVoronoiMappingBuffer =
            coupling.emitterVoronoiMappingBuffer != VK_NULL_HANDLE
            ? coupling.emitterVoronoiMappingBuffer
            : simRuntime.getInjectionKBuffer();
        const VkDeviceSize emitterVoronoiMappingBufferOffset =
            coupling.emitterVoronoiMappingBuffer != VK_NULL_HANDLE
            ? coupling.emitterVoronoiMappingBufferOffset
            : simRuntime.getInjectionKBufferOffset();

        VkDescriptorBufferInfo bufferInfos[9] = {
            { tempReadBuffer, tempReadOffset, sizeof(float) * simRuntime.getNodeCount() },
            { coupling.paramsBuffer, coupling.paramsBufferOffset, sizeof(HeatContactParams) },
            { simRuntime.getInjectionKBuffer(), simRuntime.getInjectionKBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
            { simRuntime.getInjectionKTBuffer(), simRuntime.getInjectionKTBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
            { coupling.contactSampleBuffer, coupling.contactSampleBufferOffset, sizeof(ContactSampleGPU) * coupling.contactSampleCount },
            { coupling.contactCellMapBuffer, coupling.contactCellMapBufferOffset, sizeof(ContactCellMap) * coupling.contactCellMapCount },
            { coupling.contactCellRangeBuffer, coupling.contactCellRangeBufferOffset, sizeof(ContactCellRange) * coupling.contactCellRangeCount },
            { emitterTriangleIndexBuffer, emitterTriangleIndexBufferOffset, VK_WHOLE_SIZE },
            { emitterVoronoiMappingBuffer, emitterVoronoiMappingBufferOffset, VK_WHOLE_SIZE },
        };

        VkWriteDescriptorSet writes[9]{};
        for (uint32_t binding = 0; binding < 9; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = descriptorSet;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType =
                (binding == 1) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[binding].pBufferInfo = &bufferInfos[binding];
        }
        vkUpdateDescriptorSets(context.vulkanDevice.getDevice(), 9, writes, 0, nullptr);
    };

    updateSet(coupling.contactComputeSetA, simRuntime.getTempBufferA(), simRuntime.getTempBufferAOffset());
    updateSet(coupling.contactComputeSetB, simRuntime.getTempBufferB(), simRuntime.getTempBufferBOffset());

    coupling.contactDescriptorsReady = true;
}

void HeatSystemContactStage::dispatchCoupling(
    VkCommandBuffer commandBuffer,
    const HeatContactRuntime::CouplingState& coupling,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    bool evenSubstep) const {
    if (!coupling.contactDescriptorsReady || coupling.contactCellRangeCount == 0) {
        return;
    }

    VkDescriptorSet descriptorSet = evenSubstep ? coupling.contactComputeSetA : coupling.contactComputeSetB;
    if (descriptorSet == VK_NULL_HANDLE ||
        context.resources.contactPipeline == VK_NULL_HANDLE ||
        context.resources.contactPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    const HeatSystemRuntime::SourceBinding* sourceBinding =
        findSourceBindingByRuntimeModelId(sourceBindings, coupling.emitterModelId);
    if (!sourceBinding || !sourceBinding->heatSource) {
        return;
    }

    ContactPushConstant pushConstant{};
    pushConstant.couplingKind =
        (coupling.couplingType == ContactCouplingType::ReceiverToReceiver) ? 1u : 0u;
    pushConstant.heatSourceTemperature = sourceBinding->heatSource->getUniformTemperature();

    const uint32_t workGroupSize = 256;
    const uint32_t workGroupCount = (coupling.contactCellRangeCount + workGroupSize - 1) / workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.resources.contactPipeline);
    vkCmdPushConstants(
        commandBuffer,
        context.resources.contactPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ContactPushConstant),
        &pushConstant);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        context.resources.contactPipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
}

void HeatSystemContactStage::insertInjectionBarrier(
    VkCommandBuffer commandBuffer,
    const HeatSystemSimRuntime& simRuntime) const {
    VkBufferMemoryBarrier barriers[2]{};

    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].buffer = simRuntime.getInjectionKBuffer();
    barriers[0].offset = simRuntime.getInjectionKBufferOffset();
    barriers[0].size = VK_WHOLE_SIZE;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].buffer = simRuntime.getInjectionKTBuffer();
    barriers[1].offset = simRuntime.getInjectionKTBufferOffset();
    barriers[1].size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        2,
        barriers,
        0,
        nullptr);
}
