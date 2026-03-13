#include "HeatSystemContactStage.hpp"

#include "HeatSystemResources.hpp"

#include "HeatContactParams.hpp"
#include "HeatReceiver.hpp"
#include "HeatSource.hpp"
#include "util/file_utils.h"
#include "mesh/remesher/Remesher.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

HeatSystemContactStage::HeatSystemContactStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

bool HeatSystemContactStage::ensureParamsBuffer(HeatSystemRuntime::ContactCoupling& coupling) {
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

void HeatSystemContactStage::refreshCouplings() {
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
    poolSizes[0].descriptorCount = totalSets * 10;
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
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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

void HeatSystemContactStage::updateCouplingDescriptors(HeatSystemRuntime::ContactCoupling& coupling, uint32_t nodeCount) {
    coupling.contactDescriptorsReady = false;
    if (nodeCount == 0 || !coupling.receiver) {
        return;
    }

    HeatReceiver* const receiver = coupling.receiver;
    if (!ensureParamsBuffer(coupling)) {
        return;
    }

    if (receiver->getTriangleIndicesBuffer() == VK_NULL_HANDLE ||
        receiver->getVoronoiMappingBuffer() == VK_NULL_HANDLE ||
        coupling.contactPairBuffer == VK_NULL_HANDLE ||
        receiver->getIntrinsicTriangleCount() == 0) {
        return;
    }

    VkBuffer sourceSurfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize sourceSurfaceOffset = 0;
    VkDeviceSize sourceSurfaceRange = 0;

    VkBuffer sourceTriangleBuffer = VK_NULL_HANDLE;
    VkDeviceSize sourceTriangleOffset = 0;
    VkDeviceSize sourceTriangleRange = 0;
    VkDeviceSize receiverTriangleIndexRange = sizeof(uint32_t) * 3ull * receiver->getIntrinsicTriangleCount();
    VkDeviceSize contactPairRange = sizeof(ContactPairGPU) * coupling.contactPairCount;

    VkBuffer emitterTriangleIndexBuffer = receiver->getTriangleIndicesBuffer();
    VkDeviceSize emitterTriangleIndexOffset = receiver->getTriangleIndicesBufferOffset();
    VkDeviceSize emitterTriangleIndexRange = sizeof(uint32_t) * 3ull * receiver->getIntrinsicTriangleCount();

    VkBuffer emitterVoronoiMappingBuffer = receiver->getVoronoiMappingBuffer();
    VkDeviceSize emitterVoronoiMappingOffset = receiver->getVoronoiMappingBufferOffset();
    VkDeviceSize emitterVoronoiMappingRange = sizeof(VoronoiSurfaceMapping) * receiver->getIntrinsicVertexCount();

    if (coupling.kind == ContactCouplingKind::SourceToReceiver) {
        HeatSource* source = coupling.source;
        if (!source ||
            source->getSourceBuffer() == VK_NULL_HANDLE ||
            source->getTriangleGeometryBuffer() == VK_NULL_HANDLE ||
            source->getVertexCount() == 0 ||
            source->getTriangleCount() == 0) {
            return;
        }

        sourceSurfaceBuffer = source->getSourceBuffer();
        sourceSurfaceOffset = source->getSourceBufferOffset();
        sourceSurfaceRange = sizeof(SurfacePoint) * source->getVertexCount();

        sourceTriangleBuffer = source->getTriangleGeometryBuffer();
        sourceTriangleOffset = source->getTriangleGeometryBufferOffset();
        sourceTriangleRange = sizeof(HeatSourceTriangleGPU) * source->getTriangleCount();
    } else {
        HeatReceiver* emitterReceiver = coupling.emitterReceiver;
        if (!emitterReceiver ||
            emitterReceiver == receiver ||
            emitterReceiver->getSurfaceBuffer() == VK_NULL_HANDLE ||
            emitterReceiver->getTriangleIndicesBuffer() == VK_NULL_HANDLE ||
            emitterReceiver->getVoronoiMappingBuffer() == VK_NULL_HANDLE ||
            emitterReceiver->getIntrinsicVertexCount() == 0 ||
            emitterReceiver->getIntrinsicTriangleCount() == 0) {
            return;
        }

        sourceSurfaceBuffer = emitterReceiver->getSurfaceBuffer();
        sourceSurfaceOffset = emitterReceiver->getSurfaceBufferOffset();
        sourceSurfaceRange = sizeof(SurfacePoint) * emitterReceiver->getIntrinsicVertexCount();

        // Unused in receiver->receiver mode, but must still be a valid descriptor.
        sourceTriangleBuffer = emitterReceiver->getTriangleIndicesBuffer();
        sourceTriangleOffset = emitterReceiver->getTriangleIndicesBufferOffset();
        sourceTriangleRange = sizeof(uint32_t) * 3ull * emitterReceiver->getIntrinsicTriangleCount();

        emitterTriangleIndexBuffer = emitterReceiver->getTriangleIndicesBuffer();
        emitterTriangleIndexOffset = emitterReceiver->getTriangleIndicesBufferOffset();
        emitterTriangleIndexRange = sizeof(uint32_t) * 3ull * emitterReceiver->getIntrinsicTriangleCount();

        emitterVoronoiMappingBuffer = emitterReceiver->getVoronoiMappingBuffer();
        emitterVoronoiMappingOffset = emitterReceiver->getVoronoiMappingBufferOffset();
        emitterVoronoiMappingRange = sizeof(VoronoiSurfaceMapping) * emitterReceiver->getIntrinsicVertexCount();
    }

    if (sourceSurfaceRange == 0 ||
        sourceTriangleRange == 0 ||
        receiverTriangleIndexRange == 0 ||
        contactPairRange == 0 ||
        emitterTriangleIndexRange == 0 ||
        emitterVoronoiMappingRange == 0) {
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

        VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        if (vkAllocateDescriptorSets(context.vulkanDevice.getDevice(), &allocInfo, sets) != VK_SUCCESS) {
            std::cerr << "[HeatSystem] Failed to allocate contact coupling descriptor sets" << std::endl;
            return;
        }

        coupling.contactComputeSetA = sets[0];
        coupling.contactComputeSetB = sets[1];
    }

    VkDescriptorSet sets[2] = { coupling.contactComputeSetA, coupling.contactComputeSetB };
    VkBuffer tempBuffers[2] = { context.resources.tempBufferA, context.resources.tempBufferB };
    VkDeviceSize tempOffsets[2] = { context.resources.tempBufferAOffset_, context.resources.tempBufferBOffset_ };
    for (uint32_t pass = 0; pass < 2; ++pass) {
        std::array<VkDescriptorBufferInfo, 11> infos = {
            VkDescriptorBufferInfo{tempBuffers[pass], tempOffsets[pass], sizeof(float) * nodeCount},
            VkDescriptorBufferInfo{receiver->getTriangleIndicesBuffer(), receiver->getTriangleIndicesBufferOffset(), receiverTriangleIndexRange},
            VkDescriptorBufferInfo{sourceSurfaceBuffer, sourceSurfaceOffset, sourceSurfaceRange},
            VkDescriptorBufferInfo{sourceTriangleBuffer, sourceTriangleOffset, sourceTriangleRange},
            VkDescriptorBufferInfo{coupling.paramsBuffer, coupling.paramsBufferOffset, sizeof(HeatContactParams)},
            VkDescriptorBufferInfo{receiver->getVoronoiMappingBuffer(), receiver->getVoronoiMappingBufferOffset(), sizeof(VoronoiSurfaceMapping) * receiver->getIntrinsicVertexCount()},
            VkDescriptorBufferInfo{context.resources.injectionKBuffer, context.resources.injectionKBufferOffset_, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{context.resources.injectionKTBuffer, context.resources.injectionKTBufferOffset_, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{coupling.contactPairBuffer, coupling.contactPairBufferOffset, contactPairRange},
            VkDescriptorBufferInfo{emitterTriangleIndexBuffer, emitterTriangleIndexOffset, emitterTriangleIndexRange},
            VkDescriptorBufferInfo{emitterVoronoiMappingBuffer, emitterVoronoiMappingOffset, emitterVoronoiMappingRange},
        };

        std::array<VkWriteDescriptorSet, 11> writes{};
        for (uint32_t i = 0; i < static_cast<uint32_t>(writes.size()); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = sets[pass];
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = (i == 4u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(context.vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    coupling.contactDescriptorsReady = true;
}
