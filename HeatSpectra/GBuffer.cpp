#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <array>
#include <vector>

#include "Structs.hpp"
#include "File_utils.h"
#include "Model.hpp"
#include "Grid.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"
#include "VulkanImage.hpp"
#include "UniformBufferManager.hpp"
#include "ResourceManager.hpp"
#include "DeferredRenderer.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "GBuffer.hpp"

GBuffer::GBuffer(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, HeatSystem& heatSystem,
    uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight, bool drawWireframe)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), deferredRenderer(deferredRenderer), resourceManager(resourceManager), heatSystem(heatSystem), uniformBufferManager(uniformBufferManager) {

    createFramebuffers(vulkanDevice, deferredRenderer, swapChainImageViews, swapchainExtent, maxFramesInFlight);

    createGeometryDescriptorPool(vulkanDevice, maxFramesInFlight);
    createGeometryDescriptorSetLayout(vulkanDevice);
    createGeometryDescriptorSets(vulkanDevice, resourceManager, uniformBufferManager, maxFramesInFlight);

    createLightingDescriptorPool(vulkanDevice, maxFramesInFlight);
    createLightingDescriptorSetLayout(vulkanDevice);
    createLightingDescriptorSets(vulkanDevice, deferredRenderer, uniformBufferManager, maxFramesInFlight);

    createBlendDescriptorPool(vulkanDevice, maxFramesInFlight);
    createBlendDescriptorSetLayout(vulkanDevice);
    createBlendDescriptorSets(vulkanDevice, deferredRenderer, maxFramesInFlight);

    createGeometryPipeline(vulkanDevice, deferredRenderer, swapchainExtent);
    createLightingPipeline(vulkanDevice, deferredRenderer, swapchainExtent);
    createWireframePipeline(vulkanDevice, deferredRenderer, swapchainExtent);
    createIntrinsicOverlayPipeline(vulkanDevice, deferredRenderer, swapchainExtent);
    createBlendPipeline(vulkanDevice, deferredRenderer, swapchainExtent);

    createCommandBuffers(vulkanDevice, maxFramesInFlight);
}

GBuffer::~GBuffer() {
}

void GBuffer::createCommandBuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    gbufferCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanDevice.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(gbufferCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, gbufferCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for G-buffer pass");
    }
}

void GBuffer::freeCommandBuffers(VulkanDevice& vulkanDevice) {
    vkFreeCommandBuffers(vulkanDevice.getDevice(), vulkanDevice.getCommandPool(), static_cast<uint32_t>(gbufferCommandBuffers.size()), gbufferCommandBuffers.data());
    gbufferCommandBuffers.clear();
}

void GBuffer::createFramebuffers(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight) {
    size_t totalFramebuffers = maxFramesInFlight * swapChainImageViews.size();
    framebuffers.resize(totalFramebuffers);

    if (swapChainImageViews.empty()) {
        throw std::runtime_error("Swapchain image views array is empty");
    }

    for (size_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
        for (size_t swapchainIndex = 0; swapchainIndex < swapChainImageViews.size(); swapchainIndex++) {

            std::array<VkImageView, 11> attachments = {
            deferredRenderer.getAlbedoViews()[frameIndex],         // Albedo 
            deferredRenderer.getNormalViews()[frameIndex],         // Normal 
            deferredRenderer.getPositionViews()[frameIndex],       // Position 
            deferredRenderer.getDepthViews()[frameIndex],          // Depth 
            deferredRenderer.getAlbedoResolveViews()[frameIndex],  // Albedo Resolve
            deferredRenderer.getNormalResolveViews()[frameIndex],  // Normal Resolve
            deferredRenderer.getPositionResolveViews()[frameIndex],// Position Resolve
            deferredRenderer.getDepthResolveViews()[frameIndex],   // Depth Resolve
            swapChainImageViews[swapchainIndex],                   // Swapchain
            deferredRenderer.getGridViews()[frameIndex],           // Grid 
            deferredRenderer.getGridResolveViews()[frameIndex]     // Grid Resolve
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = deferredRenderer.getRenderPass();
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            size_t framebufferIndex = frameIndex * swapChainImageViews.size() + swapchainIndex;  // Calculate the framebuffer index
            if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffers[framebufferIndex]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create G-buffer framebuffer");
            }
        }
    }
}

void GBuffer::updateDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, uint32_t maxFramesInFlight) {
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Update G-buffer descriptors (for input attachments)
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = deferredRenderer.getAlbedoResolveViews()[i];
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = deferredRenderer.getNormalResolveViews()[i];
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = deferredRenderer.getPositionResolveViews()[i];
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Albedo input binding
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1; // Normal input binding
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2; // Position input binding
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        //descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        //descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        //descriptorWrites[3].dstBinding = 3; // Depth input binding
        //descriptorWrites[3].dstArrayElement = 0;
        //descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        //descriptorWrites[3].descriptorCount = 1;
        //descriptorWrites[3].pImageInfo = &depthImageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorImageInfo gridResolveImageInfo{};
        gridResolveImageInfo.imageView = deferredRenderer.getGridResolveViews()[i];
        gridResolveImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet blendWrite{};
        blendWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blendWrite.dstSet = blendDescriptorSets[i];
        blendWrite.dstBinding = 0;
        blendWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        blendWrite.descriptorCount = 1;
        blendWrite.pImageInfo = &gridResolveImageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &blendWrite, 0, nullptr);
    }
}

void GBuffer::createGeometryDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = static_cast<uint32_t>(maxFramesInFlight);

    std::array<VkDescriptorPoolSize, 1> poolSizes = { uboPoolSize };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight);

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &geometryDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor pool!");
    }
}

void GBuffer::createGeometryDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0; // UBO binding index
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // Used in vertex and frag shader
    uboBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 1> bindings = { uboBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &geometryDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor set layout!");
    }
    std::cout << "Created geometry descriptor set layout: " << geometryDescriptorSetLayout << std::endl;
}

void GBuffer::createGeometryDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, geometryDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = geometryDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    geometryDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, geometryDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set!");
    }
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // UBO descriptor
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);


        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};

        // Write descriptor for the UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = geometryDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Binding for UBO
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createLightingDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 3;  // Albedo, Normal, Position
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 2;  // UBO, Light UBO

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight); // One descriptor set per frame in flight

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &lightingDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool for lighting pass");
    }
}

void GBuffer::createLightingDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Albedo input attachment
        {0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Normal input attachment
        {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Position input attachment
        {2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        //Depth input attachment
        //{3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // UBO
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Light UBO
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &lightingDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor set layout");
    }
    std::cout << "Created lighting descriptor set layout: " << lightingDescriptorSetLayout << std::endl;
}

void GBuffer::createLightingDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, lightingDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = lightingDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    lightingDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, lightingDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets for lighting pass");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // G-buffer descriptors (for input attachments)
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = deferredRenderer.getAlbedoResolveViews()[i];     // Albedo resolve view
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = deferredRenderer.getNormalResolveViews()[i];     // Normal resolve view
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = deferredRenderer.getPositionResolveViews()[i]; // Position resolve view
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        //VkDescriptorImageInfo depthImageInfo{};
        //depthImageInfo.imageView = gDepthImageViews[i]; // Gbuffer depth image view
        //depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        // Main UBO descriptor
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        // Light UBO descriptor
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = uniformBufferManager.getLightBuffers()[i];
        lightBufferInfo.offset = uniformBufferManager.getLightBufferOffsets()[i];
        lightBufferInfo.range = sizeof(LightUniformBufferObject);

        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        // Write descriptors for the G-buffer inputs
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Albedo input binding
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1; // Normal input binding
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2; // Position input binding
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        /*descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3; // Depth input binding
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &depthImageInfo;
        */
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        descriptorWrites[3].dstBinding = 4; // Main UBO input binding
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &uboBufferInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = lightingDescriptorSets[i];
        descriptorWrites[4].dstBinding = 5; // Light UBO input binding
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &lightBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createBlendDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSize.descriptorCount = maxFramesInFlight * 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &blendDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend descriptor pool");
    }
}

void GBuffer::createBlendDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &blendDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend descriptor set layout");
    }
}

void GBuffer::createBlendDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, blendDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = blendDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    blendDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, blendDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate blend descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = deferredRenderer.getGridResolveViews()[i];
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = blendDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void GBuffer::createGeometryPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/gbuffer_vert.spv");
    auto fragShaderCode = readFile("shaders/gbuffer_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();
    auto surfaceVertexAttributes = Vertex::getSurfaceVertexAttributes();

    // Set vertex binding descriptions
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    // Combine vertex and surface attributes into a single vector
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.insert(attributeDescriptions.end(), vertexAttributes.begin(), vertexAttributes.end());
    attributeDescriptions.insert(attributeDescriptions.end(), surfaceVertexAttributes.begin(), surfaceVertexAttributes.end());

    // Set vertex attribute descriptions
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; //debug
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.minSampleShading = 1.0f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; // Match renderpass

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;

    depthStencil.stencilTestEnable = VK_TRUE;

    // Front face stencil operations
    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_REPLACE;             // Replace stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;                // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;           // Keep stencil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;           // Always pass stencil test
    stencilOp.compareMask = 0xFF;                         // Compare all bits
    stencilOp.writeMask = 0xFF;                           // Write all bits
    stencilOp.reference = 1;                              // Write value 1 (0x01)

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};
    for (int i = 0; i < 3; ++i) {
        colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3; // Number of gbuffer attachments
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR,
         VK_DYNAMIC_STATE_DEPTH_BIAS
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &geometryDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &geometryPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = geometryPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // Geometry pass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &geometryPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createLightingPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D swapchainExtent) {
    auto vertShaderCode = readFile("shaders/lighting_vert.spv");
    auto fragShaderCode = readFile("shaders/lighting_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP; // Keep stenvil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_EQUAL;  // Pass if stencil value equals reference
    stencilOp.compareMask = 0xFF;               // Compare all bits
    stencilOp.writeMask = 0x00;                 // Write no bits
    stencilOp.reference = 1;                    // Read value 1 

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &lightingDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &lightingPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = lightingPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 1; // Lighting subpass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &lightingPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
}

void GBuffer::createWireframePipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent) {
    auto vertCode = readFile("shaders/wireframe_vert.spv");
    auto fragCode = readFile("shaders/wireframe_frag.spv");

    VkShaderModule vertModule = createShaderModule(vulkanDevice, vertCode);
    VkShaderModule fragModule = createShaderModule(vulkanDevice, fragCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex input 
    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    std::vector<VkVertexInputAttributeDescription> positionAttribute = { vertexAttributes[0] };

    // Set vertex attribute descriptions
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(positionAttribute.size());
    vertexInputInfo.pVertexAttributeDescriptions = positionAttribute.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state 
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Proper rasterization setup
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; // Match renderpass
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth stencil should match geometry pipeline
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; // Overlay
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP; // Keep stenvil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_EQUAL;  // Pass if stencil value equals reference
    stencilOp.compareMask = 0xFF;               // Compare all bits
    stencilOp.writeMask = 0x00;                 // Write no bits
    stencilOp.reference = 1;                    // Read value 1

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    // Color blending should match geometry subpass
    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};

    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    // Dont write color to normal attachment
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;

    // Dont write color to position attachment
    colorBlendAttachments[2].colorWriteMask = 0;
    colorBlendAttachments[2].blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_LINE_WIDTH
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &geometryDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &wireframePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create wireframe pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = wireframePipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // Geometry subpass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create wireframe pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void GBuffer::createIntrinsicOverlayPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/intrinsicOverlay_vert.spv"); 
    auto fragShaderCode = readFile("shaders/intrinsicOverlay_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Reuse Model vertex layout
    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();
    auto surfaceVertexAttributes = Vertex::getSurfaceVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.insert(attributeDescriptions.end(), vertexAttributes.begin(), vertexAttributes.end());
    attributeDescriptions.insert(attributeDescriptions.end(), surfaceVertexAttributes.begin(), surfaceVertexAttributes.end());

    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; 
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Disable culling - render both sides
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;  // Enable for dynamic depth bias control

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; 

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;    // Enable depth write for proper occlusion
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color attachments: match geometry's attachments count and setup (or only write to albedo)
    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};
    for (int i = 0; i < 3; ++i) {
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_TRUE;
        colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR,
         VK_DYNAMIC_STATE_DEPTH_BIAS
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &geometryDescriptorSetLayout; // Reuse geometry descriptor set layout
    layoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicOverlayPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create overlay line pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicOverlayPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // geometry pass so overlay renders with geometry attachments

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicOverlayPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create overlay line pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createBlendPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent) {
    auto vertCode = readFile("shaders/blend_vert.spv");
    auto fragCode = readFile("shaders/blend_frag.spv");

    VkShaderModule vertModule = createShaderModule(vulkanDevice, vertCode);
    VkShaderModule fragModule = createShaderModule(vulkanDevice, fragCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &blendDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &blendPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = blendPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 3;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &blendPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

VkFormat GBuffer::findDepthFormat(VkPhysicalDevice physicalDevice) {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(vulkanDevice.getPhysicalDevice(), format, &props);

        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find a suitable depth format");
}

bool GBuffer::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

const char* formatToString(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";

    default: return "Unknown Format";
    }
}

void logImageDetails(VulkanDevice& vulkanDevice, VkImage image, VkImageCreateInfo imageInfo) {
    // Log image dimension and format
    std::cout << "Image Details:\n";
    std::cout << "  Dimensions: " << imageInfo.extent.width << "x"
        << imageInfo.extent.height << "x" << imageInfo.extent.depth << "\n";
    std::cout << "  Format: " << formatToString(imageInfo.format) << "\n";

    // Retrieve memory requirements
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), image, &memRequirements);
    std::cout << "  Memory Requirements:\n";
    std::cout << "    Size: " << memRequirements.size / (1024.0f * 1024.0f) << "MB\n";
}

void GBuffer::recordCommandBuffer(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, std::vector<VkImageView> swapChainImageViews, uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent, bool drawWireframe, bool drawCommonSubdivision) {
    VkCommandBuffer commandBuffer = gbufferCommandBuffers[imageIndex];

    // Start recording commands  
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(gbufferCommandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording gbuffer command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = deferredRenderer.getRenderPass();
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    VkSubpassBeginInfo subpassBeginInfo{};
    subpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    subpassBeginInfo.contents = VK_SUBPASS_CONTENTS_INLINE;

    VkSubpassBeginInfo nextSubpassBeginInfo{};
    nextSubpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    VkSubpassEndInfo nextSubpassEndInfo{};
    nextSubpassEndInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO;

    std::array<VkClearValue, 11> clearValues{};
    clearValues[0].color = { {clearColorValues[0], clearColorValues[1], clearColorValues[2], 1.0f } };  // Albedo 
    clearValues[1].color = { { 0.0f, 0.0f, 1.0f, 0.0f } };  // Normal 
    clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Position 
    clearValues[3].depthStencil = { 1.0f, 0 };              // Depth 
    clearValues[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Albedo Resolve
    clearValues[5].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Normal Resolve
    clearValues[6].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Position Resolve
    clearValues[7].depthStencil = { 1.0f, 0 };              // Depth Resolve
    clearValues[8].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };  // Swapchain
    clearValues[9].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Grid
    clearValues[10].color = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // Grid Resolve

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass2(commandBuffer, &renderPassInfo, &subpassBeginInfo);

    // Geometry subpass
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Draw visModel 
    VkBuffer visIndexBuffer = resourceManager.getVisModel().getIndexBuffer();
    /*
    std::cout << "[RECORD] frame " << currentFrame
        << " vkCmdBindVertexBuffers: VB=" << resourceManager.getVisModel().getVertexBuffer()
        << ", offset=" << resourceManager.getVisModel().getVertexBufferOffset() << "\n";
    std::cout << "[RECORD] frame " << currentFrame
        << " vkCmdBindIndexBuffer:  IB=" << visIndexBuffer
        << ", offset=" << resourceManager.getVisModel().getIndexBufferOffset() << "\n";
    */
    // Draw input model first (pushed furthest from camera)
    VkBuffer vertexBuffers[] = { resourceManager.getVisModel().getVertexBuffer(), resourceManager.getVisModel().getSurfaceVertexBuffer() };
    VkDeviceSize vertexOffsets[] = {
    resourceManager.getVisModel().getVertexBufferOffset(),
    resourceManager.getVisModel().getSurfaceVertexBufferOffset()
    };
    
    // Push vismodel furthest away from camera 
    vkCmdSetDepthBias(commandBuffer, 2.0f, 0.0f, 2.0f);
    
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, visIndexBuffer, resourceManager.getVisModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getVisModel().getIndices().size()), 1, 0, 0, 0);
    
    // Reset depth bias
    vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);

    // Draw commonsub model on top with depth bias 
    if (drawCommonSubdivision) {
        VkBuffer intrinsicBuffers[] = { resourceManager.getCommonSubdivision().getVertexBuffer(), resourceManager.getCommonSubdivision().getSurfaceVertexBuffer() };
        VkDeviceSize intrinsicOffsets[] = {
        resourceManager.getCommonSubdivision().getVertexBufferOffset(),
        resourceManager.getCommonSubdivision().getSurfaceVertexBufferOffset()
        };
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicOverlayPipeline);
        
        // Push commonsub model away from camera 
        vkCmdSetDepthBias(commandBuffer, 1.0f, 0.0f, 0.5f);
        
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, intrinsicBuffers, intrinsicOffsets);
        vkCmdBindIndexBuffer(commandBuffer, resourceManager.getCommonSubdivision().getIndexBuffer(), resourceManager.getCommonSubdivision().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicOverlayPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getCommonSubdivision().getIndices().size()), 1, 0, 0, 0);
        
        // Reset depth bias after common subdivision
        vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);
    }
    
    // Draw wireframe on top
    if (drawWireframe) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframePipeline);
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, resourceManager.getVisModel().getIndexBuffer(), resourceManager.getVisModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getVisModel().getIndices().size()), 1, 0, 0, 0);
    }

    // Draw heat source model
    VkBuffer heatBuffers[] = { resourceManager.getHeatModel().getVertexBuffer(), resourceManager.getHeatModel().getSurfaceVertexBuffer() };
    VkDeviceSize heatOffsets[] = {
    resourceManager.getHeatModel().getVertexBufferOffset(),
    resourceManager.getHeatModel().getSurfaceVertexBufferOffset()
    };
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline);
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, heatBuffers, heatOffsets);
    vkCmdBindIndexBuffer(commandBuffer, resourceManager.getHeatModel().getIndexBuffer(), resourceManager.getHeatModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getHeatModel().getIndices().size()), 1, 0, 0, 0);

    // Transition to lighting subpass
    vkCmdNextSubpass2(commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipelineLayout, 0, 1, &lightingDescriptorSets[currentFrame], 0, nullptr);
    // Draw fullscreen triangle
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    // Transition to grid subpass
    vkCmdNextSubpass2(commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipelineLayout(), 0, 1, &resourceManager.getGrid().getGridDescriptorSets()[currentFrame], 0, nullptr);
    // Draw grid
    vkCmdDraw(commandBuffer, resourceManager.getGrid().vertexCount, 1, 0, 0);

    // Transition to blend subpass
    vkCmdNextSubpass2(commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipelineLayout, 0, 1, &blendDescriptorSets[currentFrame], 0, nullptr);
    // Draw swapchain blend
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record G-buffer command buffer");
    }
}

void GBuffer::cleanupFramebuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    for (uint32_t i = 0; i < framebuffers.size(); ++i) {
        if (framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffers[i], nullptr);
            framebuffers[i] = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();
}

void GBuffer::cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    resourceManager.getGrid().cleanup(vulkanDevice);

    vkDestroyPipeline(vulkanDevice.getDevice(), geometryPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), lightingPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), wireframePipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicOverlayPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), blendPipeline, nullptr);

    vkDestroyPipelineLayout(vulkanDevice.getDevice(), geometryPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), lightingPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), wireframePipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicOverlayPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), blendPipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), lightingDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), geometryDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), blendDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(vulkanDevice.getDevice(), lightingDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), geometryDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), blendDescriptorPool, nullptr);
}