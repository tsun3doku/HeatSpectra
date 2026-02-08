#include "LloydCompute.hpp"

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanBuffer.hpp"
#include "CommandBufferManager.hpp"
#include "VulkanImage.hpp"
#include "file_utils.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace {
    struct LloydParamsCPU {
        uint32_t nodeCount;
        float alpha;
        float maxStep;
        float pad0;
    };
}

LloydCompute::LloydCompute(VulkanDevice& device, MemoryAllocator& allocator, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), commandPool(cmdPool) {
}

LloydCompute::~LloydCompute() {
    cleanup();
}

void LloydCompute::initialize(uint32_t newNodeCount) {
    if (initialized) {
        // Only resize buffers if needed
        if (newNodeCount != nodeCount) {
            createBuffers(newNodeCount);
        }
        return;
    }

    nodeCount = newNodeCount;

    createDescriptorSetLayout();
    createDescriptorPool();
    createBuffers(nodeCount);
    createDescriptorSet();

    createAccumulatePipeline();
    createUpdatePipeline();

    initialized = true;
}

void LloydCompute::updateDescriptors(const Bindings& bindings) {
    currentBindings = bindings;

    if (!initialized || descriptorSet == VK_NULL_HANDLE)
        return;

    VkDeviceSize voxelParamsRange = currentBindings.voxelGridParamsBufferRange;
    if (voxelParamsRange == 0) {
        voxelParamsRange = VK_WHOLE_SIZE;
    }

    std::array<VkDescriptorBufferInfo, 9> infos = {
        VkDescriptorBufferInfo{currentBindings.seedPositionBuffer, currentBindings.seedPositionBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.meshTriangleBuffer, currentBindings.meshTriangleBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelTrianglesListBuffer, currentBindings.voxelTrianglesListBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelOffsetsBuffer, currentBindings.voxelOffsetsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.neighborIndicesBuffer, currentBindings.neighborIndicesBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.seedFlagsBuffer, currentBindings.seedFlagsBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{lloydAccumBuffer, lloydAccumBufferOffset, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{currentBindings.voxelGridParamsBuffer, currentBindings.voxelGridParamsBufferOffset, voxelParamsRange},
        VkDescriptorBufferInfo{lloydParamsBuffer, lloydParamsBufferOffset, sizeof(LloydParamsCPU)},
    };

    std::array<VkWriteDescriptorSet, 9> writes{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(writes.size()); i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i == 7 || i == 8) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }

    vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void LloydCompute::dispatch(int numIterations, float alpha, float maxStep) {
    if (!initialized || nodeCount == 0 || descriptorSet == VK_NULL_HANDLE)
        return;

    if (mappedLloydParamsData) {
        LloydParamsCPU p{ nodeCount, alpha, maxStep, 0.0f };
        std::memcpy(mappedLloydParamsData, &p, sizeof(LloydParamsCPU));
    }

    VkCommandBuffer cmd = commandPool.beginCommands();

    uint32_t workGroupSize = 64;
    uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;

    for (int iter = 0; iter < numIterations; iter++) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            accumulatePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, workGroupCount, 1, 1);

        VkMemoryBarrier barrierA{};
        barrierA.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrierA.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrierA.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrierA, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updatePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            updatePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, workGroupCount, 1, 1);

        VkMemoryBarrier barrierB{};
        barrierB.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrierB.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrierB.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrierB, 0, nullptr, 0, nullptr);
    }

    commandPool.endCommands(cmd);
}

void LloydCompute::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute descriptor set layout");
    }
}

void LloydCompute::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 7;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute descriptor pool");
    }
}

void LloydCompute::createBuffers(uint32_t newNodeCount) {
    if (lloydAccumBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(lloydAccumBuffer, lloydAccumBufferOffset);
        lloydAccumBuffer = VK_NULL_HANDLE;
        mappedLloydAccumData = nullptr;
    }

    nodeCount = newNodeCount;

    VkDeviceSize accumSize = sizeof(float) * 4 * nodeCount;
    createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, accumSize, lloydAccumBuffer, lloydAccumBufferOffset, &mappedLloydAccumData);

    if (lloydParamsBuffer == VK_NULL_HANDLE) {
        VkDeviceSize paramsSize = sizeof(LloydParamsCPU);
        createUniformBuffer(memoryAllocator, vulkanDevice, paramsSize, lloydParamsBuffer, lloydParamsBufferOffset, &mappedLloydParamsData);
        if (mappedLloydParamsData)
            std::memset(mappedLloydParamsData, 0, static_cast<size_t>(paramsSize));
    }
}

void LloydCompute::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate LloydCompute descriptor set");
    }
}

void LloydCompute::createAccumulatePipeline() {
    auto code = readFile("shaders/lloyd_accumulate_comp.spv");
    VkShaderModule module = createShaderModule(vulkanDevice, code);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &accumulatePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute accumulate pipeline layout");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = accumulatePipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &accumulatePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute accumulate pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), module, nullptr);
}

void LloydCompute::createUpdatePipeline() {
    auto code = readFile("shaders/lloyd_update_comp.spv");
    VkShaderModule module = createShaderModule(vulkanDevice, code);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &updatePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute update pipeline layout");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = updatePipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &updatePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create LloydCompute update pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), module, nullptr);
}

void LloydCompute::cleanupResources() {
    if (accumulatePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), accumulatePipeline, nullptr);
        accumulatePipeline = VK_NULL_HANDLE;
    }
    if (accumulatePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), accumulatePipelineLayout, nullptr);
        accumulatePipelineLayout = VK_NULL_HANDLE;
    }
    if (updatePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), updatePipeline, nullptr);
        updatePipeline = VK_NULL_HANDLE;
    }
    if (updatePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), updatePipelineLayout, nullptr);
        updatePipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    initialized = false;
}

void LloydCompute::cleanup() {
    if (lloydAccumBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(lloydAccumBuffer, lloydAccumBufferOffset);
        lloydAccumBuffer = VK_NULL_HANDLE;
        mappedLloydAccumData = nullptr;
    }
    if (lloydParamsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(lloydParamsBuffer, lloydParamsBufferOffset);
        lloydParamsBuffer = VK_NULL_HANDLE;
        mappedLloydParamsData = nullptr;
    }

    cleanupResources();
}
