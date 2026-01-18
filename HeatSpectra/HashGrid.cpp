#include "HashGrid.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanBuffer.hpp"
#include "CommandBufferManager.hpp"
#include "file_utils.h"
#include "VulkanImage.hpp"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <array>
#include <fstream>
#include <vector>

HashGrid::HashGrid(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,
                   CommandPool& cmdPool, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), 
      cmdPool(cmdPool), maxFramesInFlight(maxFramesInFlight) {
}

HashGrid::~HashGrid() {
    cleanup();
}

void HashGrid::initialize(const glm::vec3& minBounds, const glm::vec3& maxBounds, float cellSize, uint32_t maxPointsPerCell) {
    if (isInitialized) {
        cleanupResources();
    }
    
    params.gridMin = minBounds;
    params.cellSize = cellSize;
    params.maxPointsPerCell = maxPointsPerCell;
    
    glm::vec3 gridExtent = maxBounds - minBounds;
    params.gridDimensions = glm::ivec3(
        static_cast<int>(std::ceil(gridExtent.x / cellSize)),
        static_cast<int>(std::ceil(gridExtent.y / cellSize)),
        static_cast<int>(std::ceil(gridExtent.z / cellSize))
    );
    
    params.gridDimensions = glm::min(params.gridDimensions, glm::ivec3(128));
    params.gridDimensions = glm::max(params.gridDimensions, glm::ivec3(1));
    
    params.totalCells = params.gridDimensions.x * params.gridDimensions.y * params.gridDimensions.z;
    
    std::cout << "[HashGrid] Initialized " << params.gridDimensions.x << "x" 
              << params.gridDimensions.y << "x" << params.gridDimensions.z 
              << " (" << params.totalCells << " cells)" << std::endl;
    
    uint32_t maxPointsTotal = params.totalCells * maxPointsPerCell;
    
    createBuffers(maxPointsTotal);
    createBuildDescriptorSetLayout();
    createBuildDescriptorPool(maxFramesInFlight);
    createBuildDescriptorSets(maxFramesInFlight);
    createBuildPipeline();
    
    isInitialized = true;
    perFrameNeedsRebuild.resize(maxFramesInFlight, true); 
}

void HashGrid::createBuffers(uint32_t maxPointsTotal) {
VkDeviceSize gridBufferSize = sizeof(GridPoint) * maxPointsTotal;
    void* mappedPtr;
    createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, gridBufferSize,
                       gridBuffer, gridBufferOffset_, &mappedPtr, false);
    
    VkDeviceSize cellStartBufferSize = sizeof(uint32_t) * params.totalCells;
    createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, cellStartBufferSize,
                       cellStartBuffer, cellStartBufferOffset_, &mappedPtr, false);
    
    VkDeviceSize cellCountBufferSize = sizeof(uint32_t) * params.totalCells;
    createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, cellCountBufferSize,
                       cellCountBuffer, cellCountBufferOffset_, &mappedPtr, false, 
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    
    VkDeviceSize paramsBufferSize = sizeof(HashGridParams);
    createUniformBuffer(memoryAllocator, vulkanDevice, paramsBufferSize,
                       paramsBuffer, paramsBufferOffset_, reinterpret_cast<void**>(&mappedParams));
    
    if (mappedParams) {
        *mappedParams = params;
    }
    
    // Create occupied cell buffers per frame
    VkDeviceSize occupiedCellsBufferSize = sizeof(uint32_t) * params.totalCells;
    VkDeviceSize occupiedCountBufferSize = sizeof(uint32_t);
    
    occupiedCellsBuffers.resize(maxFramesInFlight);
    occupiedCellsBufferOffsets.resize(maxFramesInFlight);
    occupiedCountBuffers.resize(maxFramesInFlight);
    occupiedCountBufferOffsets.resize(maxFramesInFlight);
    mappedOccupiedCounts.resize(maxFramesInFlight);
    indirectDrawBuffers.resize(maxFramesInFlight);
    indirectDrawBufferOffsets.resize(maxFramesInFlight);
    
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, occupiedCellsBufferSize,
                           occupiedCellsBuffers[i], occupiedCellsBufferOffsets[i], &mappedPtr, false);
        
        createStorageBuffer(memoryAllocator, vulkanDevice, nullptr, occupiedCountBufferSize,
                           occupiedCountBuffers[i], occupiedCountBufferOffsets[i], &mappedPtr, true,
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        mappedOccupiedCounts[i] = mappedPtr;
        
        // Create indirect draw buffer 
        VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
        auto [indirectBuffer, indirectOffset] = memoryAllocator.allocate(indirectBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        indirectDrawBuffers[i] = indirectBuffer;
        indirectDrawBufferOffsets[i] = indirectOffset;
    }
}

void HashGrid::updateBuildDescriptors(VkBuffer sourceBuffer, VkDeviceSize sourceOffset, uint32_t frameIndex) {
    if (!isInitialized || frameIndex >= buildDescriptorSets.size()) 
        return;
    
    // Update descriptor set with input buffer (binding 0)
    VkDescriptorBufferInfo sourceBufferInfo{};
    sourceBufferInfo.buffer = sourceBuffer;
    sourceBufferInfo.offset = sourceOffset;
    sourceBufferInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = buildDescriptorSets[frameIndex];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &sourceBufferInfo;
    
    vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &descriptorWrite, 0, nullptr);
}

void HashGrid::buildGrid(VkCommandBuffer cmdBuffer,VkBuffer sourceBuffer, uint32_t sourceCount, VkDeviceSize sourceOffset, const glm::mat4& transform, uint32_t frameIndex) {
    if (!isInitialized) {
        std::cerr << "[HashGrid] Not initialized" << std::endl;
        return;
    }
    
    if (frameIndex >= maxFramesInFlight) {
        std::cerr << "[HashGrid] Invalid frame index: " << frameIndex << std::endl;
        return;
    }
    
    // Skip rebuild if transform didnt change for this frame
    if (!perFrameNeedsRebuild[frameIndex] && transform == lastTransform) {
        return;
    }
    
    // Clear cell counts and occupied count for this frame
    vkCmdFillBuffer(cmdBuffer, cellCountBuffer, cellCountBufferOffset_, 
                   sizeof(uint32_t) * params.totalCells, 0);
    vkCmdFillBuffer(cmdBuffer, occupiedCountBuffers[frameIndex], occupiedCountBufferOffsets[frameIndex],
                   sizeof(uint32_t), 0);
    
    // Clear indirect draw command buffer 
    vkCmdFillBuffer(cmdBuffer, indirectDrawBuffers[frameIndex], indirectDrawBufferOffsets[frameIndex],
                   sizeof(VkDrawIndirectCommand), 0);
    
    // Memory barrier
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    
    struct BuildPushConstants {
        glm::mat4 transform;
        uint32_t pointCount;
    } pushConstants;
    
    pushConstants.transform = transform;
    pushConstants.pointCount = sourceCount;
    
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, buildPipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           buildPipelineLayout, 0, 1, &buildDescriptorSets[frameIndex], 0, nullptr);
    vkCmdPushConstants(cmdBuffer, buildPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(BuildPushConstants), &pushConstants);
    
    uint32_t workGroupCount = (sourceCount + 255) / 256;
    vkCmdDispatch(cmdBuffer, workGroupCount, 1, 1);
    
    // Cache transform and mark this frame as clean
    lastTransform = transform;
    perFrameNeedsRebuild[frameIndex] = false;
    
    // If transform changed, mark all other frames dirty so they rebuild with new transform
    for (size_t i = 0; i < perFrameNeedsRebuild.size(); i++) {
        if (i != frameIndex) {
            perFrameNeedsRebuild[i] = true;
        }
    }
}

uint32_t HashGrid::getOccupiedCellCount(uint32_t frameIndex) const {
    if (frameIndex < mappedOccupiedCounts.size() && mappedOccupiedCounts[frameIndex]) {
        return *static_cast<uint32_t*>(mappedOccupiedCounts[frameIndex]);
    }
    return 0;
}

void HashGrid::recreateResources(uint32_t maxFramesInFlight) {
    if (!isInitialized) return;
    
    std::fill(perFrameNeedsRebuild.begin(), perFrameNeedsRebuild.end(), true);
    
    if (buildDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), buildDescriptorPool, nullptr);
    }
    
    createBuildDescriptorPool(maxFramesInFlight);
    createBuildDescriptorSets(maxFramesInFlight);
}

void HashGrid::cleanupResources() {
    VkDevice device = vulkanDevice.getDevice();
    
    if (buildPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, buildPipeline, nullptr);
        buildPipeline = VK_NULL_HANDLE;
    }
    if (buildPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, buildPipelineLayout, nullptr);
        buildPipelineLayout = VK_NULL_HANDLE;
    }
    if (buildDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, buildDescriptorSetLayout, nullptr);
        buildDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (buildDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, buildDescriptorPool, nullptr);
        buildDescriptorPool = VK_NULL_HANDLE;
    }
    
    if (gridBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gridBuffer, gridBufferOffset_);
        gridBuffer = VK_NULL_HANDLE;
    }
    if (cellStartBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(cellStartBuffer, cellStartBufferOffset_);
        cellStartBuffer = VK_NULL_HANDLE;
    }
    if (cellCountBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(cellCountBuffer, cellCountBufferOffset_);
        cellCountBuffer = VK_NULL_HANDLE;
    }
    if (paramsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(paramsBuffer, paramsBufferOffset_);
        paramsBuffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < occupiedCellsBuffers.size(); ++i) {
        if (occupiedCellsBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(occupiedCellsBuffers[i], occupiedCellsBufferOffsets[i]);
        }
    }
    occupiedCellsBuffers.clear();
    occupiedCellsBufferOffsets.clear();
    
    for (size_t i = 0; i < occupiedCountBuffers.size(); ++i) {
        if (occupiedCountBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(occupiedCountBuffers[i], occupiedCountBufferOffsets[i]);
        }
    }
    occupiedCountBuffers.clear();
    occupiedCountBufferOffsets.clear();
    mappedOccupiedCounts.clear();
    
    for (size_t i = 0; i < indirectDrawBuffers.size(); ++i) {
        if (indirectDrawBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(indirectDrawBuffers[i], indirectDrawBufferOffsets[i]);
        }
    }
    indirectDrawBuffers.clear();
    indirectDrawBufferOffsets.clear();
    
    isInitialized = false;
}

void HashGrid::cleanup() {
    cleanupResources();
}

void HashGrid::createBuildDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    
    // Binding 0: Source buffer (input)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 1: Grid buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 2: Cell start buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 3: Cell count buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 4: Occupied cells buffer
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 5: Params buffer
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 6: Occupied count buffer
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 7: Indirect draw command buffer
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr,
        &buildDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid build descriptor set layout");
    }
}

void HashGrid::createBuildDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 7;  // 7 storage buffers per frame
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;
    
    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr,
        &buildDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid build descriptor pool");
    }
}

void HashGrid::createBuildDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, buildDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = buildDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    buildDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo,
        buildDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate hash grid build descriptor sets");
    }
    
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorBufferInfo, 7> bufferInfos{};
        std::array<VkWriteDescriptorSet, 7> descriptorWrites{};
        
        // Binding 1: Grid buffer
        bufferInfos[0].buffer = gridBuffer;
        bufferInfos[0].offset = gridBufferOffset_;
        bufferInfos[0].range = VK_WHOLE_SIZE;
        
        // Binding 2: Cell start buffer
        bufferInfos[1].buffer = cellStartBuffer;
        bufferInfos[1].offset = cellStartBufferOffset_;
        bufferInfos[1].range = VK_WHOLE_SIZE;
        
        // Binding 3: Cell count buffer
        bufferInfos[2].buffer = cellCountBuffer;
        bufferInfos[2].offset = cellCountBufferOffset_;
        bufferInfos[2].range = VK_WHOLE_SIZE;
        
        // Binding 4: Occupied cells buffer 
        bufferInfos[3].buffer = occupiedCellsBuffers[i];
        bufferInfos[3].offset = occupiedCellsBufferOffsets[i];
        bufferInfos[3].range = VK_WHOLE_SIZE;
        
        // Binding 5: Params uniform buffer
        bufferInfos[4].buffer = paramsBuffer;
        bufferInfos[4].offset = paramsBufferOffset_;
        bufferInfos[4].range = sizeof(HashGridParams);
        
        // Binding 6: Occupied count buffer 
        bufferInfos[5].buffer = occupiedCountBuffers[i];
        bufferInfos[5].offset = occupiedCountBufferOffsets[i];
        bufferInfos[5].range = VK_WHOLE_SIZE;
        
        // Binding 7: Indirect draw command buffer
        bufferInfos[6].buffer = indirectDrawBuffers[i];
        bufferInfos[6].offset = indirectDrawBufferOffsets[i];
        bufferInfos[6].range = sizeof(VkDrawIndirectCommand);
        
        for (int j = 0; j < 7; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = buildDescriptorSets[i];
            descriptorWrites[j].dstBinding = j + 1;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = (j == 4) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER 
                                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pBufferInfo = &bufferInfos[j];
        }
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 
                              static_cast<uint32_t>(descriptorWrites.size()),
                              descriptorWrites.data(), 0, nullptr);
    }
}

void HashGrid::createBuildPipeline() {
    auto computeShaderCode = readFile("shaders/hash_grid_build_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);
    
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";
    
    struct BuildPushConstants {
        glm::mat4 transform;
        uint32_t pointCount;
    };
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BuildPushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &buildDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &buildPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid build pipeline layout");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = buildPipelineLayout;
    
    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &buildPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid build pipeline");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
    std::cout << "[HashGrid] Build pipeline created" << std::endl;
}
