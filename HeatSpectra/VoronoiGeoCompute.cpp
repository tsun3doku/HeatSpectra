#include "VoronoiGeoCompute.hpp"

#include "VulkanDevice.hpp"
#include "CommandBufferManager.hpp"
#include "VulkanImage.hpp"
#include "file_utils.h"

#include <array>
#include <vector>
#include <stdexcept>

VoronoiGeoCompute::VoronoiGeoCompute(VulkanDevice& device, CommandPool& cmdPool)
    : vulkanDevice(device), commandPool(cmdPool) {
}

VoronoiGeoCompute::~VoronoiGeoCompute() {
    cleanup();
}

void VoronoiGeoCompute::initialize(uint32_t newNodeCount) {
    nodeCount = newNodeCount;

    if (initialized) {
        return;
    }

    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    createPipeline();

    initialized = true;
}

void VoronoiGeoCompute::updateDescriptors(const Bindings& bindings) {
    currentBindings = bindings;

    if (!initialized || descriptorSet == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize nodeRange = currentBindings.voronoiNodeBufferRange;
    if (nodeRange == 0) {
        nodeRange = VK_WHOLE_SIZE;
    }

    VkDeviceSize voxelParamsRange = currentBindings.voxelGridParamsBufferRange;
    if (voxelParamsRange == 0) {
        voxelParamsRange = VK_WHOLE_SIZE;
    }

    std::array<VkDescriptorBufferInfo, 17> infos = {
        VkDescriptorBufferInfo{currentBindings.voronoiNodeBuffer, currentBindings.voronoiNodeBufferOffset, nodeRange},
        VkDescriptorBufferInfo{currentBindings.meshTriangleBuffer, currentBindings.meshTriangleBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.seedPositionBuffer, currentBindings.seedPositionBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.relevantMeshIndicesBuffer, currentBindings.relevantMeshIndicesBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.spatialInfoBuffer, currentBindings.spatialInfoBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelGridParamsBuffer, currentBindings.voxelGridParamsBufferOffset, voxelParamsRange},
        VkDescriptorBufferInfo{currentBindings.voxelOccupancyBuffer, currentBindings.voxelOccupancyBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelMeshPointsBuffer, currentBindings.voxelMeshPointsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelMeshTrianglesBuffer, currentBindings.voxelMeshTrianglesBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelTrianglesListBuffer, currentBindings.voxelTrianglesListBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelOffsetsBuffer, currentBindings.voxelOffsetsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.neighborIndicesBuffer, currentBindings.neighborIndicesBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.interfaceAreasBuffer, currentBindings.interfaceAreasBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.interfaceNeighborIdsBuffer, currentBindings.interfaceNeighborIdsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.debugCellGeometryBuffer, currentBindings.debugCellGeometryBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.seedFlagsBuffer, currentBindings.seedFlagsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voronoiDumpBuffer, currentBindings.voronoiDumpBufferOffset, VK_WHOLE_SIZE},
    };

    std::array<VkWriteDescriptorSet, 17> writes{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(writes.size()); i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i == 5) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }

    vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VoronoiGeoCompute::dispatch() {
    if (!initialized || nodeCount == 0 || descriptorSet == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate VoronoiGeoCompute command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkMemoryBarrier uploadBarrier{};
    uploadBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    uploadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    uploadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &uploadBarrier, 0, nullptr, 0, nullptr);

    uint32_t workGroupSize = 32;
    uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, workGroupCount, 1, 1);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
        throw std::runtime_error("Failed to create VoronoiGeoCompute fence");
    }

    VkQueue computeQueue = vulkanDevice.getComputeQueue();
    vkQueueSubmit(computeQueue, 1, &submitInfo, fence);
    vkWaitForFences(vulkanDevice.getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(vulkanDevice.getDevice(), fence, nullptr);
    vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool.getHandle(), 1, &cmd);
}

void VoronoiGeoCompute::cleanupResources() {
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

void VoronoiGeoCompute::cleanup() {
    cleanupResources();
}

void VoronoiGeoCompute::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VoronoiGeoCompute descriptor set layout");
    }
}

void VoronoiGeoCompute::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 16;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VoronoiGeoCompute descriptor pool");
    }
}

void VoronoiGeoCompute::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate VoronoiGeoCompute descriptor set");
    }
}

void VoronoiGeoCompute::createPipeline() {
    auto computeShaderCode = readFile("shaders/heat_geometry_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VoronoiGeoCompute pipeline layout");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VoronoiGeoCompute pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
}
