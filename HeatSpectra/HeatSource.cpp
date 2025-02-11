#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <tiny_obj_loader.h>

#include "VulkanDevice.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"
#include "HeatSource.hpp"

void HeatSource::init(VulkanDevice& vulkanDevice, Model& heatModel, uint32_t maxFramesInFlight) {
    this->vulkanDevice = &vulkanDevice;
    this->heatModel = &heatModel;
    
    createSourceBuffer(vulkanDevice, heatModel);

    createHeatSourceDescriptorPool(vulkanDevice, maxFramesInFlight);
    createHeatSourceDescriptorSetLayout(vulkanDevice);
    createHeatSourceDescriptorSets(vulkanDevice, maxFramesInFlight);
    createHeatSourcePipeline(vulkanDevice);
}

void HeatSource::createSourceBuffer(VulkanDevice& vulkanDevice, Model& heatModel) {
    // Create CPU-side initialized data
    std::vector<HeatSourceVertex> vertices;
    const auto& modelVertices = heatModel.getVertices();

    for (const auto& v : modelVertices) {
        vertices.push_back({
            v.pos,
            glm::vec3(1.0f, 0.0f, 0.0f), // Initial color (red)
            10.0f // Initial temperature
            });
    }

    VkDeviceSize bufferSize = sizeof(HeatSourceVertex) * vertices.size();
    std::cout << "Source buffer size: " << bufferSize << "\n";

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    stagingBuffer = vulkanDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory
    );

    // Map and copy data
    void* data;
    vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);

    VkCommandBuffer copyCmd = beginSingleTimeCommands(vulkanDevice);

    VkBufferCopy region{ 0, 0, bufferSize };
    
    vkCmdCopyBuffer(copyCmd, stagingBuffer, heatModel.getSurfaceBuffer(), 1, &region);
    
    vkCmdCopyBuffer(copyCmd, stagingBuffer, heatModel.getSurfaceVertexBuffer(), 1, &region);

    endSingleTimeCommands(vulkanDevice, copyCmd);

    // 5) Cleanup staging
    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
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
        surfaceBufferInfo.offset = 0;
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

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &heatSourceDescriptorLayout;

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
    vkDestroyBuffer(vulkanDevice.getDevice(), sourceBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), sourceBufferMemory, nullptr);
}

