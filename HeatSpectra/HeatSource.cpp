#include <vulkan/vulkan.h>
#include <tiny_obj_loader.h>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"
#include "HeatSource.hpp"

HeatSource::HeatSource(VulkanDevice& device, MemoryAllocator& allocator, Model& model, uint32_t maxFramesInFlight, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), heatModel(model), renderCommandPool(cmdPool), maxFramesInFlight(maxFramesInFlight) {

    createSourceBuffer();
    initializeSurfaceBuffer();
    createHeatSourceDescriptorPool(maxFramesInFlight);
    createHeatSourceDescriptorSetLayout();
    createHeatSourceDescriptorSets(maxFramesInFlight);
    createHeatSourcePipeline();
}

HeatSource::~HeatSource() {
}

void HeatSource::recreateResources(uint32_t maxFramesInFlight) {
    createHeatSourceDescriptorPool(maxFramesInFlight);
    createHeatSourceDescriptorSetLayout();
    createHeatSourcePipeline();
    createHeatSourceDescriptorSets(maxFramesInFlight);
}

void HeatSource::createSourceBuffer() {
    VkDeviceSize bufferSize = sizeof(HeatSourceVertex) * heatModel.getVertexCount();

    // Allocate staging buffer
    auto [stagingBufferHandle, stagingBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Initialize surface vertices
    std::vector<HeatSourceVertex> surfaceVertices(heatModel.getVertexCount());
    const auto& modelVertices = heatModel.getVertices();
    for (size_t i = 0; i < heatModel.getVertexCount(); i++) {
        surfaceVertices[i].position = glm::vec4(modelVertices[i].pos, 1.0f);
        surfaceVertices[i].temperature = 100.0f;
    }

    // Copy data to staging buffer
    void* mapped = memoryAllocator.getMappedPointer(stagingBufferHandle, stagingBufferOffset);
    memcpy(mapped, surfaceVertices.data(), static_cast<size_t>(bufferSize));

    // Allocate source buffer
    auto [sourceBufferHandle, sourceBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    sourceBuffer = sourceBufferHandle;
    sourceBufferOffset_ = sourceBufferOffset;

    // Copy buffer with offsets using render command pool
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy region{};
    region.srcOffset = stagingBufferOffset;
    region.dstOffset = sourceBufferOffset_;
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBufferHandle, sourceBuffer, 1, &region);
    renderCommandPool.endCommands(cmd);

    // Free staging buffer
    memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
}

void HeatSource::initializeSurfaceBuffer() {
    VkDeviceSize bufferSize = sizeof(SurfaceVertex) * heatModel.getVertexCount();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    stagingBuffer = vulkanDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingMemory
    );

    // Initialize surface vertices
    std::vector<SurfaceVertex> surfaceVertices(heatModel.getVertexCount());
    const auto& modelVerts = heatModel.getVertices();
    for (size_t i = 0; i < modelVerts.size(); i++) {
        surfaceVertices[i].position = glm::vec4(modelVerts[i].pos, 1.0);
        surfaceVertices[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Shader uses base vertex data
    }

    // Copy data to staging buffer
    void* data;
    vkMapMemory(vulkanDevice.getDevice(), stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, surfaceVertices.data(), (size_t)bufferSize);
    vkUnmapMemory(vulkanDevice.getDevice(), stagingMemory);

    // Copy from staging to actual buffer using render command pool
    VkCommandBuffer cmd = renderCommandPool.beginCommands();

    // Copy to surface buffer with offset
    VkBufferCopy copyRegionSurface{
        0,                                     // Source offset
        heatModel.getSurfaceBufferOffset(),   // Destination offset
        bufferSize                             // Size
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, heatModel.getSurfaceBuffer(), 1, &copyRegionSurface);

    // Copy to surface vertex buffer with offset
    VkBufferCopy copyRegionVertex{
        0,                                         // Source offset
        heatModel.getSurfaceVertexBufferOffset(), // Destination offset
        bufferSize                                 // Size
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, heatModel.getSurfaceVertexBuffer(), 1, &copyRegionVertex);

    renderCommandPool.endCommands(cmd);

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingMemory, nullptr);
}

void HeatSource::controller(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, float deltaTime) {
    float moveSpeed = 0.1f * deltaTime;
    glm::vec3 currentPosition = heatModel.getModelPosition();

    if (upPressed)
        currentPosition += glm::vec3(0.0f, moveSpeed, 0.0f);
    if (downPressed)
        currentPosition -= glm::vec3(0.0f, moveSpeed, 0.0f);
    if (rightPressed)
        currentPosition += glm::vec3(moveSpeed, 0.0f, 0.0f);
    if (leftPressed)
        currentPosition -= glm::vec3(moveSpeed, 0.0f, 0.0f);

    heatModel.setModelPosition(currentPosition);
}

void HeatSource::createHeatSourceDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};

    // Storage buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 2;  // Storage buffer count

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &heatSourceDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source descriptor pool!");
    }
}

void HeatSource::createHeatSourceDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Source binding
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Surface binding
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &heatSourceDescriptorLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source descriptor set layout!");
    }
}

void HeatSource::createHeatSourceDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, heatSourceDescriptorLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = heatSourceDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    heatSourceDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, heatSourceDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate heat source descriptor sets!");
    }

    // Update descriptor sets
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorBufferInfo sourceBufferInfo{};
        sourceBufferInfo.buffer = sourceBuffer; // Read
        sourceBufferInfo.offset = 0;
        sourceBufferInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo surfaceBufferInfo{};
        surfaceBufferInfo.buffer = heatModel.getSurfaceBuffer(); // Write
        surfaceBufferInfo.offset = heatModel.getSurfaceBufferOffset();
        surfaceBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        // Write for binding=0
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = heatSourceDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &sourceBufferInfo;

        // Write for binding=1
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = heatSourceDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &surfaceBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }
}

void HeatSource::createHeatSourcePipeline() {
    auto computeShaderCode = readFile("shaders/heat_source_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(vulkanDevice, computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatSourcePushConstant);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &heatSourceDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &heatSourcePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = heatSourcePipelineLayout;

    if (vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &heatSourcePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source compute pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), computeShaderModule, nullptr);
}

void HeatSource::dispatchSourceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, heatSourcePipeline);

    vkCmdPushConstants(
        commandBuffer,
        heatSourcePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(HeatSourcePushConstant),
        &heatSourcePushConstant);

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        heatSourcePipelineLayout,
        0,
        1,
        &heatSourceDescriptorSets[currentFrame],
        0,
        nullptr
    );

    // Dispatch 1 workgroup per 256 vertices
    uint32_t workgroupCount = (static_cast<uint32_t>(heatModel.getVertexCount()) + 255) / 256;
    vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);
}

void HeatSource::cleanupResources() {
    if (heatSourcePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), heatSourcePipeline, nullptr);
        heatSourcePipeline = VK_NULL_HANDLE;
    }
    if (heatSourcePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), heatSourcePipelineLayout, nullptr);
        heatSourcePipelineLayout = VK_NULL_HANDLE;
    }
    if (heatSourceDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), heatSourceDescriptorPool, nullptr);
        heatSourceDescriptorPool = VK_NULL_HANDLE;
    }
    if (heatSourceDescriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), heatSourceDescriptorLayout, nullptr);
        heatSourceDescriptorLayout = VK_NULL_HANDLE;
    }
}

void HeatSource::cleanup() {
    if (sourceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(sourceBuffer, sourceBufferOffset_);
        sourceBuffer = VK_NULL_HANDLE;
    }
}
