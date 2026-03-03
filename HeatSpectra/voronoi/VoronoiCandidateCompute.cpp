#include "VoronoiCandidateCompute.hpp"

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/VulkanImage.hpp"
#include "util/file_utils.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
struct CandidatePushConstants {
    uint32_t faceCount;
    uint32_t seedCount;
    uint32_t _pad0;
    uint32_t _pad1;
};
}

VoronoiCandidateCompute::VoronoiCandidateCompute(VulkanDevice& device, CommandPool& cmdPool)
    : vulkanDevice(device), commandPool(cmdPool) {
}

VoronoiCandidateCompute::~VoronoiCandidateCompute() {
    cleanup();
}

void VoronoiCandidateCompute::initialize() {
    if (initialized) {
        return;
    }

    if (!createDescriptorSetLayout() ||
        !createDescriptorPool() ||
        !createDescriptorSet() ||
        !createPipeline()) {
        cleanupResources();
        return;
    }

    initialized = true;
}

void VoronoiCandidateCompute::updateDescriptors(const Bindings& bindings) {
    currentBindings = bindings;
    if (!initialized || descriptorSet == VK_NULL_HANDLE) {
        return;
    }

    std::array<VkWriteDescriptorSet, 4> writes{};
    std::array<VkDescriptorBufferInfo, 4> infos = {
        VkDescriptorBufferInfo{currentBindings.vertexBuffer, currentBindings.vertexBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.faceIndexBuffer, currentBindings.faceIndexBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.seedPositionBuffer, currentBindings.seedPositionBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.candidateBuffer, currentBindings.candidateBufferOffset, VK_WHOLE_SIZE},
    };

    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }

    vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VoronoiCandidateCompute::dispatch(uint32_t faceCount, uint32_t seedCount) {
    if (!initialized || descriptorSet == VK_NULL_HANDLE || faceCount == 0 || seedCount == 0) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, &cmd) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to allocate command buffer" << std::endl;
        return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to begin command buffer" << std::endl;
        vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
        return;
    }

    VkMemoryBarrier uploadBarrier{};
    uploadBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    uploadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    uploadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &uploadBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    CandidatePushConstants pc{};
    pc.faceCount = faceCount;
    pc.seedCount = seedCount;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CandidatePushConstants), &pc);

    uint32_t workGroupSize = 256;
    uint32_t workGroupCount = (faceCount + workGroupSize - 1) / workGroupSize;
    vkCmdDispatch(cmd, workGroupCount, 1, 1);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to end command buffer" << std::endl;
        vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
        std::cerr << "VoronoiCandidateCompute: Failed to create fence" << std::endl;
        return;
    }

    VkQueue computeQueue = vulkanDevice.getComputeQueue();
    if (vkQueueSubmit(computeQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to submit compute work" << std::endl;
        vkDestroyFence(vulkanDevice.getDevice(), fence, nullptr);
        vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
        return;
    }
    if (vkWaitForFences(vulkanDevice.getDevice(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed while waiting for fence" << std::endl;
    }

    vkDestroyFence(vulkanDevice.getDevice(), fence, nullptr);
    vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
}

void VoronoiCandidateCompute::cleanupResources() {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    descriptorSet = VK_NULL_HANDLE;
    initialized = false;
}

void VoronoiCandidateCompute::cleanup() {
    cleanupResources();
}

bool VoronoiCandidateCompute::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to create descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiCandidateCompute::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to create descriptor pool" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiCandidateCompute::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to allocate descriptor set" << std::endl;
        return false;
    }

    return true;
}

bool VoronoiCandidateCompute::createPipeline() {
    std::vector<char> computeShaderCode;
    if (!readFile("shaders/voronoi_candidates_comp.spv", computeShaderCode)) {
        std::cerr << "VoronoiCandidateCompute: Failed to read shader file" << std::endl;
        return false;
    }

    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, computeShaderCode, computeShaderModule) != VK_SUCCESS) {
        std::cerr << "VoronoiCandidateCompute: Failed to create shader module" << std::endl;
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
    pushConstantRange.size = sizeof(CandidatePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "VoronoiCandidateCompute: Failed to create pipeline layout" << std::endl;
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
        std::cerr << "VoronoiCandidateCompute: Failed to create compute pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
    return true;
}
