#include "HeatSystemVoronoiStage.hpp"

#include "HeatSystemResources.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include "util/file_utils.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <iostream>
#include <vector>

HeatSystemVoronoiStage::HeatSystemVoronoiStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemVoronoiStage::dispatchDiffusionSubstep(
    VkCommandBuffer commandBuffer,
    uint32_t currentFrame,
    const HeatSystemSimRuntime& simRuntime,
    const heat::SourcePushConstant& basePushConstant,
    int substepIndex,
    uint32_t workGroupCount) const {
    (void)simRuntime;
    const bool isEven = (substepIndex % 2 == 0);
    VkDescriptorSet voronoiSet = context.resources.voronoiDescriptorSetsB[currentFrame];
    if (isEven) {
        voronoiSet = context.resources.voronoiDescriptorSets[currentFrame];
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context.resources.voronoiPipeline);
    vkCmdPushConstants(
        commandBuffer,
        context.resources.voronoiPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(heat::SourcePushConstant),
        &basePushConstant);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        context.resources.voronoiPipelineLayout,
        0,
        1,
        &voronoiSet,
        0,
        nullptr);
    vkCmdDispatch(commandBuffer, workGroupCount, 1, 1);
}

void HeatSystemVoronoiStage::insertInterSubstepBarrier(
    VkCommandBuffer commandBuffer,
    const HeatSystemSimRuntime& simRuntime,
    int substepIndex,
    uint32_t numSubsteps) const {
    if (substepIndex >= (static_cast<int>(numSubsteps) - 1)) {
        return;
    }

    const bool isEven = (substepIndex % 2 == 0);
    VkBuffer writeBuffer = simRuntime.getTempBufferA();
    VkDeviceSize writeOffset = simRuntime.getTempBufferAOffset();
    if (isEven) {
        writeBuffer = simRuntime.getTempBufferB();
        writeOffset = simRuntime.getTempBufferBOffset();
    }

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.buffer = writeBuffer;
    barrier.offset = writeOffset;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr);
}

void HeatSystemVoronoiStage::insertFinalTemperatureBarrier(
    VkCommandBuffer commandBuffer,
    const HeatSystemSimRuntime& simRuntime,
    uint32_t numSubsteps) const {
    const bool writesBufferB = finalSubstepWritesBufferB(numSubsteps);
    VkBuffer finalTempBuffer = simRuntime.getTempBufferA();
    VkDeviceSize finalTempOffset = simRuntime.getTempBufferAOffset();
    if (writesBufferB) {
        finalTempBuffer = simRuntime.getTempBufferB();
        finalTempOffset = simRuntime.getTempBufferBOffset();
    }

    VkBufferMemoryBarrier finalTempBarrier{};
    finalTempBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    finalTempBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalTempBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalTempBarrier.buffer = finalTempBuffer;
    finalTempBarrier.offset = finalTempOffset;
    finalTempBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &finalTempBarrier,
        0,
        nullptr);
}

bool HeatSystemVoronoiStage::finalSubstepWritesBufferB(uint32_t numSubsteps) const {
    if (numSubsteps == 0) {
        return false;
    }
    return ((numSubsteps - 1) % 2 == 0);
}

bool HeatSystemVoronoiStage::createDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 2 * 7;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * 2;

    if (vkCreateDescriptorPool(
            context.vulkanDevice.getDevice(),
            &poolInfo,
            nullptr,
            &context.resources.voronoiDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemVoronoiStage::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    std::vector<VkDescriptorBindingFlags> flags(
        bindings.size(),
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

    if (vkCreateDescriptorSetLayout(
            context.vulkanDevice.getDevice(),
            &layoutInfo,
            nullptr,
            &context.resources.voronoiDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystemVoronoiStage::createDescriptorSets(uint32_t maxFramesInFlight, const HeatSystemSimRuntime& simRuntime) {
    if (context.resources.voronoiDescriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(context.vulkanDevice.getDevice(), context.resources.voronoiDescriptorPool, 0);
    }

    std::vector<VkDescriptorSetLayout> layouts(
        maxFramesInFlight * 2,
        context.resources.voronoiDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context.resources.voronoiDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight * 2;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> allSets(maxFramesInFlight * 2);
    if (vkAllocateDescriptorSets(context.vulkanDevice.getDevice(), &allocInfo, allSets.data()) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to allocate Voronoi descriptor sets" << std::endl;
        return false;
    }

    context.resources.voronoiDescriptorSets.resize(maxFramesInFlight);
    context.resources.voronoiDescriptorSetsB.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        context.resources.voronoiDescriptorSets[i] = allSets[i * 2];
        context.resources.voronoiDescriptorSetsB[i] = allSets[i * 2 + 1];
    }

    const uint32_t nodeCount = context.resources.voronoiNodeCount;
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        const VkBuffer contactConductanceBuffer =
            context.resources.contactConductanceBuffer != VK_NULL_HANDLE
                ? context.resources.contactConductanceBuffer
                : simRuntime.getTempBufferA();
        const VkDeviceSize contactConductanceOffset =
            context.resources.contactConductanceBuffer != VK_NULL_HANDLE
                ? context.resources.contactConductanceBufferOffset
                : simRuntime.getTempBufferAOffset();
        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{
                    context.resources.voronoiNodeBuffer,
                    context.resources.voronoiNodeBufferOffset,
                    sizeof(voronoi::Node) * nodeCount},
                VkDescriptorBufferInfo{
                    context.resources.gmlsInterfaceBuffer,
                    context.resources.gmlsInterfaceBufferOffset,
                    VK_WHOLE_SIZE},
                VkDescriptorBufferInfo{
                    context.resources.voronoiMaterialNodeBuffer,
                    context.resources.voronoiMaterialNodeBufferOffset,
                    sizeof(voronoi::MaterialNode) * nodeCount},
                VkDescriptorBufferInfo{
                    simRuntime.getTimeBuffer(),
                    simRuntime.getTimeBufferOffset(),
                    sizeof(heat::TimeUniform)},
                VkDescriptorBufferInfo{
                    simRuntime.getTempBufferA(),
                    simRuntime.getTempBufferAOffset(),
                    sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{
                    simRuntime.getTempBufferB(),
                    simRuntime.getTempBufferBOffset(),
                    sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{
                    context.resources.seedFlagsBuffer,
                    context.resources.seedFlagsBufferOffset,
                    sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{
                    contactConductanceBuffer,
                    contactConductanceOffset,
                    VK_WHOLE_SIZE},
            };

            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; ++j) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = context.resources.voronoiDescriptorSets[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType =
                    (j == 3) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(
                context.vulkanDevice.getDevice(),
                8,
                descriptorWrites.data(),
                0,
                nullptr);
        }

        {
            std::vector<VkDescriptorBufferInfo> bufferInfos = {
                VkDescriptorBufferInfo{
                    context.resources.voronoiNodeBuffer,
                    context.resources.voronoiNodeBufferOffset,
                    sizeof(voronoi::Node) * nodeCount},
                VkDescriptorBufferInfo{
                    context.resources.gmlsInterfaceBuffer,
                    context.resources.gmlsInterfaceBufferOffset,
                    VK_WHOLE_SIZE},
                VkDescriptorBufferInfo{
                    context.resources.voronoiMaterialNodeBuffer,
                    context.resources.voronoiMaterialNodeBufferOffset,
                    sizeof(voronoi::MaterialNode) * nodeCount},
                VkDescriptorBufferInfo{
                    simRuntime.getTimeBuffer(),
                    simRuntime.getTimeBufferOffset(),
                    sizeof(heat::TimeUniform)},
                VkDescriptorBufferInfo{
                    simRuntime.getTempBufferB(),
                    simRuntime.getTempBufferBOffset(),
                    sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{
                    simRuntime.getTempBufferA(),
                    simRuntime.getTempBufferAOffset(),
                    sizeof(float) * nodeCount},
                VkDescriptorBufferInfo{
                    context.resources.seedFlagsBuffer,
                    context.resources.seedFlagsBufferOffset,
                    sizeof(uint32_t) * nodeCount},
                VkDescriptorBufferInfo{
                    contactConductanceBuffer,
                    contactConductanceOffset,
                    VK_WHOLE_SIZE},
            };

            std::vector<VkWriteDescriptorSet> descriptorWrites(8);
            for (int j = 0; j < 8; ++j) {
                descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[j].dstSet = context.resources.voronoiDescriptorSetsB[i];
                descriptorWrites[j].dstBinding = j;
                descriptorWrites[j].descriptorCount = 1;
                descriptorWrites[j].descriptorType =
                    (j == 3) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[j].pBufferInfo = &bufferInfos[j];
            }
            vkUpdateDescriptorSets(
                context.vulkanDevice.getDevice(),
                8,
                descriptorWrites.data(),
                0,
                nullptr);
        }
    }
    return true;
}

bool HeatSystemVoronoiStage::createPipeline() {
    const auto computeShaderCode = readFile("shaders/heat_voronoi_comp.spv");
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(context.vulkanDevice, computeShaderCode, computeShaderModule) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to create Voronoi compute shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(heat::SourcePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.resources.voronoiDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(
            context.vulkanDevice.getDevice(),
            &pipelineLayoutInfo,
            nullptr,
            &context.resources.voronoiPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create Voronoi pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = context.resources.voronoiPipelineLayout;

    if (vkCreateComputePipelines(
            context.vulkanDevice.getDevice(),
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &context.resources.voronoiPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(context.vulkanDevice.getDevice(), context.resources.voronoiPipelineLayout, nullptr);
        context.resources.voronoiPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "[HeatSystem] Failed to create Voronoi compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(context.vulkanDevice.getDevice(), computeShaderModule, nullptr);
    return true;
}
