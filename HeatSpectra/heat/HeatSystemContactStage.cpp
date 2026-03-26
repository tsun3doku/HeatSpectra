#include "HeatSystemContactStage.hpp"

#include "ContactSampling.hpp"
#include "HeatReceiverRuntime.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "HeatContactParams.hpp"
#include "HeatSourceRuntime.hpp"
#include "util/file_utils.h"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

HeatSystemContactStage::HeatSystemContactStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

bool HeatSystemContactStage::ensureParamsBuffer(HeatContactRuntime::ContactCoupling& coupling) {
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

const HeatReceiverRuntime* HeatSystemContactStage::findReceiverRuntime(
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    uint32_t runtimeModelId) const {
    for (const auto& receiver : receivers) {
        if (receiver && receiver->getRuntimeModelId() == runtimeModelId) {
            return receiver.get();
        }
    }
    return nullptr;
}

const VoronoiModelRuntime* HeatSystemContactStage::findVoronoiModelRuntime(
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& voronoiModelRuntimes,
    uint32_t runtimeModelId) const {
    for (const auto& modelRuntime : voronoiModelRuntimes) {
        if (modelRuntime && modelRuntime->getRuntimeModelId() == runtimeModelId) {
            return modelRuntime.get();
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
    HeatContactRuntime::ContactCoupling& coupling,
    const HeatSystemSimRuntime& simRuntime,
    const HeatPackage& heatPackage,
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& voronoiModelRuntimes) {
    coupling.contactDescriptorsReady = false;
    (void)heatPackage;

    if (coupling.couplingType != ContactCouplingType::SourceToReceiver || !coupling.source) {
        return;
    }

    const VoronoiModelRuntime* receiverModelRuntime = findVoronoiModelRuntime(voronoiModelRuntimes, coupling.receiverModelId);
    if (!findReceiverRuntime(receivers, coupling.receiverModelId) || !receiverModelRuntime) {
        return;
    }

    const auto& triangleIndices = receiverModelRuntime->getIntrinsicTriangleIndices();
    const auto& cellIndices = receiverModelRuntime->getVoronoiSurfaceCellIndices();
    const std::size_t triangleCount = triangleIndices.size() / 3;
    const std::size_t contactPairCount = std::min(coupling.contactPairsCPU.size(), triangleCount);
    if (contactPairCount == 0 || cellIndices.empty()) {
        return;
    }

    std::vector<ContactSampleGPU> samples;
    samples.reserve(contactPairCount * Quadrature::count);

    std::vector<ContactCellWeight> cellWeights;
    cellWeights.reserve(contactPairCount * Quadrature::count * 3);

    for (std::size_t triangleIndex = 0; triangleIndex < contactPairCount; ++triangleIndex) {
        const ContactPair& contactPair = coupling.contactPairsCPU[triangleIndex];
        if (contactPair.contactArea <= 0.0f) {
            continue;
        }

        const std::size_t triangleBase = triangleIndex * 3;
        const uint32_t vertexIndices[3] = {
            triangleIndices[triangleBase + 0],
            triangleIndices[triangleBase + 1],
            triangleIndices[triangleBase + 2],
        };

        const uint32_t mappedCells[3] = {
            vertexIndices[0] < cellIndices.size() ? cellIndices[vertexIndices[0]] : std::numeric_limits<uint32_t>::max(),
            vertexIndices[1] < cellIndices.size() ? cellIndices[vertexIndices[1]] : std::numeric_limits<uint32_t>::max(),
            vertexIndices[2] < cellIndices.size() ? cellIndices[vertexIndices[2]] : std::numeric_limits<uint32_t>::max(),
        };

        for (uint32_t sampleIndex = 0; sampleIndex < Quadrature::count; ++sampleIndex) {
            const ContactSampleGPU& sample = contactPair.samples[sampleIndex];
            if (sample.sourceTriangleIndex == std::numeric_limits<uint32_t>::max() || sample.wArea <= 0.0f) {
                continue;
            }

            const uint32_t flattenedSampleIndex = static_cast<uint32_t>(samples.size());
            samples.push_back(sample);

            const glm::vec3 barycentric = Quadrature::bary[sampleIndex];
            const float baryWeights[3] = { barycentric.x, barycentric.y, barycentric.z };
            for (int cornerIndex = 0; cornerIndex < 3; ++cornerIndex) {
                const uint32_t cellIndex = mappedCells[cornerIndex];
                const float weight = sample.wArea * baryWeights[cornerIndex];
                if (cellIndex == std::numeric_limits<uint32_t>::max() || weight <= 0.0f) {
                    continue;
                }

                ContactCellWeight cellWeight{};
                cellWeight.cellIndex = cellIndex;
                cellWeight.sampleIndex = flattenedSampleIndex;
                cellWeight.weight = weight;
                cellWeights.push_back(cellWeight);
            }
        }
    }

    if (samples.empty() || cellWeights.empty()) {
        return;
    }

    std::sort(cellWeights.begin(), cellWeights.end(), [](const ContactCellWeight& lhs, const ContactCellWeight& rhs) {
        if (lhs.cellIndex != rhs.cellIndex) {
            return lhs.cellIndex < rhs.cellIndex;
        }
        return lhs.sampleIndex < rhs.sampleIndex;
    });

    std::vector<ContactCellMap> contactCellMap;
    contactCellMap.reserve(cellWeights.size());
    std::vector<ContactCellRange> contactCellRanges;
    contactCellRanges.reserve(cellWeights.size());

    std::size_t rangeStart = 0;
    while (rangeStart < cellWeights.size()) {
        const uint32_t cellIndex = cellWeights[rangeStart].cellIndex;
        std::size_t rangeEnd = rangeStart;
        while (rangeEnd < cellWeights.size() && cellWeights[rangeEnd].cellIndex == cellIndex) {
            ContactCellMap mapEntry{};
            mapEntry.sampleIndex = cellWeights[rangeEnd].sampleIndex;
            mapEntry.weight = cellWeights[rangeEnd].weight;
            contactCellMap.push_back(mapEntry);
            ++rangeEnd;
        }

        ContactCellRange range{};
        range.cellIndex = cellIndex;
        range.startIndex = static_cast<uint32_t>(rangeStart);
        range.count = static_cast<uint32_t>(rangeEnd - rangeStart);
        contactCellRanges.push_back(range);
        rangeStart = rangeEnd;
    }

    auto recreateBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset, const void* data, VkDeviceSize size) -> bool {
        if (buffer != VK_NULL_HANDLE) {
            context.memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }

        return createStorageBuffer(
                   context.memoryAllocator,
                   context.vulkanDevice,
                   data,
                   size,
                   buffer,
                   offset,
                   nullptr,
                   true) == VK_SUCCESS &&
            buffer != VK_NULL_HANDLE;
    };

    if (!recreateBuffer(
            coupling.contactSampleBuffer,
            coupling.contactSampleBufferOffset,
            samples.data(),
            sizeof(ContactSampleGPU) * samples.size()) ||
        !recreateBuffer(
            coupling.contactCellMapBuffer,
            coupling.contactCellMapBufferOffset,
            contactCellMap.data(),
            sizeof(ContactCellMap) * contactCellMap.size()) ||
        !recreateBuffer(
            coupling.contactCellRangeBuffer,
            coupling.contactCellRangeBufferOffset,
            contactCellRanges.data(),
            sizeof(ContactCellRange) * contactCellRanges.size()) ||
        !ensureParamsBuffer(coupling)) {
        return;
    }

    coupling.contactSampleCount = static_cast<uint32_t>(samples.size());
    coupling.contactCellMapCount = static_cast<uint32_t>(contactCellMap.size());
    coupling.contactCellRangeCount = static_cast<uint32_t>(contactCellRanges.size());

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
        VkDescriptorBufferInfo bufferInfos[9] = {
            { tempReadBuffer, tempReadOffset, sizeof(float) * simRuntime.getNodeCount() },
            { coupling.paramsBuffer, coupling.paramsBufferOffset, sizeof(HeatContactParams) },
            { simRuntime.getInjectionKBuffer(), simRuntime.getInjectionKBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
            { simRuntime.getInjectionKTBuffer(), simRuntime.getInjectionKTBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
            { coupling.contactSampleBuffer, coupling.contactSampleBufferOffset, sizeof(ContactSampleGPU) * coupling.contactSampleCount },
            { coupling.contactCellMapBuffer, coupling.contactCellMapBufferOffset, sizeof(ContactCellMap) * coupling.contactCellMapCount },
            { coupling.contactCellRangeBuffer, coupling.contactCellRangeBufferOffset, sizeof(ContactCellRange) * coupling.contactCellRangeCount },
            { simRuntime.getInjectionKBuffer(), simRuntime.getInjectionKBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
            { simRuntime.getInjectionKBuffer(), simRuntime.getInjectionKBufferOffset(), sizeof(uint32_t) * simRuntime.getNodeCount() },
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
    const HeatContactRuntime::ContactCoupling& coupling,
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

    ContactPushConstant pushConstant{};
    pushConstant.couplingKind =
        (coupling.couplingType == ContactCouplingType::ReceiverToReceiver) ? 1u : 0u;
    if (coupling.source) {
        pushConstant.heatSourceTemperature = coupling.source->getUniformTemperature();
    }

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
