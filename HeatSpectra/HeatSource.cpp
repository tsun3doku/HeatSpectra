#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <tiny_obj_loader.h>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"
#include "HeatSource.hpp"

void HeatSource::init(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, uint32_t maxFramesInFlight) {
    this->vulkanDevice = &vulkanDevice;
    this->memoryAllocator = &memoryAllocator;
    this->heatModel = &heatModel;
    
    createSourceBuffer(vulkanDevice, heatModel);

    createHeatSourceDescriptorPool(vulkanDevice, maxFramesInFlight);
    createHeatSourceDescriptorSetLayout(vulkanDevice);
    createHeatSourceDescriptorSets(vulkanDevice, maxFramesInFlight);
    createHeatSourcePipeline(vulkanDevice);
}

void HeatSource::createSourceBuffer(VulkanDevice& vulkanDevice, Model& heatModel) {
    VkDeviceSize bufferSize = sizeof(HeatSourceVertex) * heatModel.getVertexCount();

    // Allocate staging buffer
    auto [stagingBufferHandle, stagingBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Initialize surface vertices
    std::vector<HeatSourceVertex> surfaceVertices(heatModel.getVertexCount());
    const auto& modelVertices = heatModel.getVertices();
    for (size_t i = 0; i < heatModel.getVertexCount(); i++) {
        surfaceVertices[i].position = modelVertices[i].pos;
        surfaceVertices[i].temperature = 9.5f;
    }

    // Copy data to staging buffer
    void* mapped = memoryAllocator->getMappedPointer(stagingBufferHandle, stagingBufferOffset);
    memcpy(mapped, surfaceVertices.data(), static_cast<size_t>(bufferSize));

    // Allocate source buffer
    auto [sourceBufferHandle, sourceBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    sourceBuffer = sourceBufferHandle;
    sourceBufferOffset_ = sourceBufferOffset;

    // Copy buffer with offsets
    VkCommandBuffer cmd = beginSingleTimeCommands(vulkanDevice);
    VkBufferCopy region{};
    region.srcOffset = stagingBufferOffset;
    region.dstOffset = sourceBufferOffset_;
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBufferHandle, sourceBuffer, 1, &region);
    endSingleTimeCommands(vulkanDevice, cmd);

    // Free staging buffer
    memoryAllocator->free(stagingBufferHandle, stagingBufferOffset);
}

void HeatSource::initializeSurfaceBuffer(Model& heatModel) {
    VkDeviceSize bufferSize = sizeof(SurfaceVertex) * heatModel.getVertexCount();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    stagingBuffer = vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingMemory
    );

    // Initialize surface vertices
    std::vector<SurfaceVertex> surfaceVertices(heatModel.getVertexCount());
    const auto& modelVerts = heatModel.getVertices();
    for (size_t i = 0; i < modelVerts.size(); i++) {
        surfaceVertices[i].position = modelVerts[i].pos;
        surfaceVertices[i].color = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // Copy data to staging buffer
    void* mapped = nullptr;
    vkMapMemory(vulkanDevice->getDevice(), stagingMemory, 0, bufferSize, 0, &mapped);
    memcpy(mapped, surfaceVertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(vulkanDevice->getDevice(), stagingMemory);

    VkCommandBuffer cmd = beginSingleTimeCommands(*vulkanDevice);

    // Copy to surface buffer with offset
    VkBufferCopy copyRegionSurface{
        0,                                    // Source offset
        heatModel.getSurfaceBufferOffset(),   // Destination offset
        bufferSize                            // Size
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, heatModel.getSurfaceBuffer(), 1, &copyRegionSurface);

    // Copy to surface vertex buffer with offset
    VkBufferCopy copyRegionVertex{
        0,                                        // Source offset
        heatModel.getSurfaceVertexBufferOffset(), // Destination offset
        bufferSize                                // Size
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, heatModel.getSurfaceVertexBuffer(), 1, &copyRegionVertex);

    endSingleTimeCommands(*vulkanDevice, cmd);

    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingMemory, nullptr);
}

void HeatSource::controller(GLFWwindow* window, float deltaTime) {
    float moveSpeed = 0.1f * deltaTime; 
    glm::vec3 currentPosition = heatModel->getModelPosition();

    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        currentPosition += glm::vec3(0.0f, moveSpeed, 0.0f);
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        currentPosition -= glm::vec3(0.0f, moveSpeed, 0.0f);
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        currentPosition += glm::vec3(moveSpeed, 0.0f, 0.0f);
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        currentPosition -= glm::vec3(moveSpeed, 0.0f, 0.0f);

    heatModel->setModelPosition(currentPosition);
}

void HeatSource::createHeatSourceDescriptorPool(VulkanDevice& device, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};

    // Storage buffers
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * 2;  // Storage buffer count

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(device.getDevice(), &poolInfo, nullptr, &heatSourceDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source descriptor pool!");
    }
}

void HeatSource::createHeatSourceDescriptorSetLayout(VulkanDevice& device) {
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

    if (vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr, &heatSourceDescriptorLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source descriptor set layout!");
    }
}

void HeatSource::createHeatSourceDescriptorSets(VulkanDevice& device, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, heatSourceDescriptorLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = heatSourceDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    heatSourceDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(device.getDevice(), &allocInfo, heatSourceDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate heat source descriptor sets!");
    }

    // Update descriptor sets
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorBufferInfo sourceBufferInfo{};
        sourceBufferInfo.buffer = sourceBuffer; // Read
        sourceBufferInfo.offset = 0;
        sourceBufferInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo surfaceBufferInfo{};
        surfaceBufferInfo.buffer = heatModel->getSurfaceBuffer(); // Write
        surfaceBufferInfo.offset = heatModel->getSurfaceBufferOffset();
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

        vkUpdateDescriptorSets(device.getDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }
}

void HeatSource::createHeatSourcePipeline(VulkanDevice& device) {
    auto computeShaderCode = readFile("shaders/heat_source_comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(device, computeShaderCode);

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

    if (vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &heatSourcePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = heatSourcePipelineLayout;

    if (vkCreateComputePipelines(device.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &heatSourcePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create heat source compute pipeline!");
    }

    vkDestroyShaderModule(device.getDevice(), computeShaderModule, nullptr);
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
    uint32_t workgroupCount = (static_cast<uint32_t>(heatModel->getVertexCount()) + 255) / 256;
    vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);
}

void HeatSource::cleanup(VulkanDevice& vulkanDevice) {
    memoryAllocator->free(sourceBuffer, sourceBufferOffset_);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), heatSourceDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), heatSourceDescriptorLayout, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), heatSourcePipeline, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), heatSourcePipelineLayout, nullptr);
}

