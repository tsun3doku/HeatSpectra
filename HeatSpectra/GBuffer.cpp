#include <stdexcept>
#include <array>
#include <vector>

#include "Structs.hpp"
#include "File_utils.h"
#include "ModelSelection.hpp"
#include "WireframeRenderer.hpp"
#include "Gizmo.hpp"
#include "Model.hpp"
#include "Grid.hpp"
#include "HeatSource.hpp"
#include "HeatReceiver.hpp"
#include "HeatSystem.hpp"
#include "VulkanImage.hpp"
#include "UniformBufferManager.hpp"
#include "iODT.hpp" 
#include "ResourceManager.hpp"
#include "DeferredRenderer.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "GBuffer.hpp"

GBuffer::GBuffer(VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager,
    uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight, CommandPool& cmdPool, bool drawWireframe)
    : vulkanDevice(vulkanDevice), deferredRenderer(deferredRenderer), resourceManager(resourceManager), renderCommandPool(cmdPool) {

    createFramebuffers(swapChainImageViews, swapchainExtent, maxFramesInFlight);

    createGeometryDescriptorPool(maxFramesInFlight);
    createGeometryDescriptorSetLayout();
    createGeometryDescriptorSets(resourceManager, uniformBufferManager, maxFramesInFlight);

    createLightingDescriptorPool(maxFramesInFlight);
    createLightingDescriptorSetLayout();
    createLightingDescriptorSets(uniformBufferManager, maxFramesInFlight);

    createBlendDescriptorPool(maxFramesInFlight);
    createBlendDescriptorSetLayout();
    createBlendDescriptorSets(maxFramesInFlight);

    createDepthSampler();
    createOutlineDescriptorPool(maxFramesInFlight);
    createOutlineDescriptorSetLayout();
    createOutlineDescriptorSets(maxFramesInFlight);
    
    // Create pool and layout for per-model descriptor sets (allocated on-demand per model)
    createSupportingHalfedgeDescriptorPool(maxFramesInFlight);
    createSupportingHalfedgeDescriptorSetLayout();
    
    // Create intrinsic normals visualization resources
    createIntrinsicNormalsDescriptorPool(maxFramesInFlight);
    createIntrinsicNormalsDescriptorSetLayout();
    
    // Create intrinsic vertex normals visualization resources
    createIntrinsicVertexNormalsDescriptorPool(maxFramesInFlight);
    createIntrinsicVertexNormalsDescriptorSetLayout();

    createGeometryPipeline(swapchainExtent);
    createLightingPipeline(swapchainExtent);
    createOutlinePipeline(swapchainExtent);
    createSupportingHalfedgePipeline(swapchainExtent);
    createIntrinsicNormalsPipeline(swapchainExtent);
    createIntrinsicVertexNormalsPipeline(swapchainExtent);
    createBlendPipeline(swapchainExtent);
    createStencilOnlyPipeline(swapchainExtent);

    createCommandBuffers(maxFramesInFlight);
}

GBuffer::~GBuffer() {
}

void GBuffer::createCommandBuffers(uint32_t maxFramesInFlight) {
    gbufferCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();  // Use render command pool
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(gbufferCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, gbufferCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for G-buffer pass");
    }
}

void GBuffer::freeCommandBuffers() {
    vkFreeCommandBuffers(vulkanDevice.getDevice(), renderCommandPool.getHandle(), 
                         static_cast<uint32_t>(gbufferCommandBuffers.size()), gbufferCommandBuffers.data());
    gbufferCommandBuffers.clear();
}

void GBuffer::createFramebuffers(std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight) {
    size_t totalFramebuffers = maxFramesInFlight * swapChainImageViews.size();
    framebuffers.resize(totalFramebuffers);

    if (swapChainImageViews.empty()) {
        throw std::runtime_error("Swapchain image views array is empty");
    }

    for (size_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
        for (size_t swapchainIndex = 0; swapchainIndex < swapChainImageViews.size(); swapchainIndex++) {

            std::array<VkImageView, 13> attachments = {
            deferredRenderer.getAlbedoViews()[frameIndex],         // 0: Albedo MSAA
            deferredRenderer.getNormalViews()[frameIndex],         // 1: Normal MSAA
            deferredRenderer.getPositionViews()[frameIndex],       // 2: Position MSAA
            deferredRenderer.getDepthViews()[frameIndex],          // 3: Depth MSAA
            deferredRenderer.getAlbedoResolveViews()[frameIndex],  // 4: Albedo Resolve
            deferredRenderer.getNormalResolveViews()[frameIndex],  // 5: Normal Resolve
            deferredRenderer.getPositionResolveViews()[frameIndex],// 6: Position Resolve
            deferredRenderer.getDepthResolveViews()[frameIndex],   // 7: Depth Resolve
            deferredRenderer.getLightingViews()[frameIndex],       // 8: Lighting MSAA
            deferredRenderer.getGridViews()[frameIndex],           // 9: Grid MSAA
            deferredRenderer.getGridResolveViews()[frameIndex],    // 10: Grid Resolve
            deferredRenderer.getLightingResolveViews()[frameIndex],// 11: Lighting Resolve
            swapChainImageViews[swapchainIndex]                    // 12: Swapchain
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = deferredRenderer.getRenderPass();
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            size_t framebufferIndex = frameIndex * swapChainImageViews.size() + swapchainIndex; 
            if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffers[framebufferIndex]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create G-buffer framebuffer");
            }
        }
    }
}

void GBuffer::updateDescriptorSets(uint32_t maxFramesInFlight) {
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
        VkDescriptorImageInfo imageInfos[2] = {};
        // Grid input
        imageInfos[0].imageView = deferredRenderer.getGridResolveViews()[i];
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Lighting input
        imageInfos[1].imageView = deferredRenderer.getLightingResolveViews()[i];
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrites[2] = {};
        // Grid binding
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = blendDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        // Lighting binding
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = blendDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 2, descriptorWrites, 0, nullptr);
    }
    
    // Update outline descriptor sets
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorImageInfo, 2> imageInfos{};
        
        // Depth texture 
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[0].imageView = deferredRenderer.getDepthResolveSamplerViews()[i];
        imageInfos[0].sampler = depthSampler;
        
        // Stencil texture
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[1].imageView = deferredRenderer.getStencilMSAASamplerViews()[i];
        imageInfos[1].sampler = depthSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = outlineDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = outlineDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createGeometryDescriptorPool(uint32_t maxFramesInFlight) {
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

void GBuffer::createGeometryDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0; // UBO binding index
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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

void GBuffer::createGeometryDescriptorSets(ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
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
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};

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

void GBuffer::createLightingDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 3;  // Albedo, Normal, Position
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 2;  // UBO, Light UBO

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight);

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &lightingDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool for lighting pass");
    }
}

void GBuffer::createLightingDescriptorSetLayout() {
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

void GBuffer::createLightingDescriptorSets(UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
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

void GBuffer::createBlendDescriptorPool(uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSize.descriptorCount = maxFramesInFlight * 2;  // Grid + Lighting inputs

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &blendDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend descriptor pool");
    }
}

void GBuffer::createBlendDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2] = {};
    // Grid input
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Lighting input
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &blendDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create blend descriptor set layout");
    }
}

void GBuffer::createBlendDescriptorSets(uint32_t maxFramesInFlight) {
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
        VkDescriptorImageInfo imageInfos[2] = {};
        // Grid input
        imageInfos[0].imageView = deferredRenderer.getGridResolveViews()[i];
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Lighting input
        imageInfos[1].imageView = deferredRenderer.getLightingResolveViews()[i];
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrites[2] = {};
        // Grid binding
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = blendDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        // Lighting binding
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = blendDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 2, descriptorWrites, 0, nullptr);
    }
}

void GBuffer::createOutlineDescriptorPool(uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxFramesInFlight * 2;  // (depth + stencil)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &outlineDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline descriptor pool");
    }
}

void GBuffer::createOutlineDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Depth texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Stencil texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &outlineDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline descriptor set layout");
    }
}

void GBuffer::createOutlineDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, outlineDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = outlineDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    outlineDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, outlineDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate outline descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorImageInfo, 2> imageInfos{};
        
        // Depth texture 
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[0].imageView = deferredRenderer.getDepthResolveSamplerViews()[i];
        imageInfos[0].sampler = depthSampler;
        
        // Stencil texture 
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[1].imageView = deferredRenderer.getStencilMSAASamplerViews()[i];
        imageInfos[1].sampler = depthSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = outlineDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = outlineDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createDepthSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; 
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth sampler");
    }
}

void GBuffer::createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Texture buffers (S, A, H, E, T, L, H_input, E_input, T_input, L_input)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels * 10;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;  
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &supportingHalfedgeDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create supporting halfedge descriptor pool");
    }
}

void GBuffer::createSupportingHalfedgeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 11> bindings{};  
    
    // Binding 0: Main UBO 
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: S buffer (supporting halfedge indices)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 2: A buffer (supporting angles)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 3: H buffer (intrinsic halfedge data)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 4: E buffer (intrinsic edge data)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 5: T buffer (intrinsic triangle data)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 6: L buffer (intrinsic edge lengths)
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 7: H_input buffer (input halfedge data)
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 8: E_input buffer (input edge data)
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 9: T_input buffer (input triangle data)
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 10: L_input buffer (input edge lengths)
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &supportingHalfedgeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create supporting halfedge descriptor set layout");
    }
}

void GBuffer::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelSupportingHalfedgeDescriptorSets.find(model) != perModelSupportingHalfedgeDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, supportingHalfedgeDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = supportingHalfedgeDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }
    
    perModelSupportingHalfedgeDescriptorSets[model] = descriptorSets;    
}

void GBuffer::updateDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelSupportingHalfedgeDescriptorSets.find(model) == perModelSupportingHalfedgeDescriptorSets.end()) {
        allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    const auto& descriptorSets = perModelSupportingHalfedgeDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 11> descriptorWrites{};
        
        // Binding 0: UBO 
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;
        
        // Bindings 1-10: Texel buffers
        VkBufferView bufferViews[10] = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView()
        };
        
        for (int j = 0; j < 10; j++) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = descriptorSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].dstArrayElement = 0;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void GBuffer::createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Storage buffer (intrinsic triangle data)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicNormalsDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic normals descriptor pool");
    }
}

void GBuffer::createIntrinsicNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Binding 0: IntrinsicTriangleData storage buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 1: UBO 
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicNormalsDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic normals descriptor set layout");
    }
}

void GBuffer::allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelIntrinsicNormalsDescriptorSets.find(model) != perModelIntrinsicNormalsDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicNormalsDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate intrinsic normals descriptor sets");
    }
    
    perModelIntrinsicNormalsDescriptorSets[model] = descriptorSets;    
}

void GBuffer::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelIntrinsicNormalsDescriptorSets.find(model) == perModelIntrinsicNormalsDescriptorSets.end()) {
        allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    VkBuffer triangleBuffer = supportingHalfedge->getIntrinsicTriangleBuffer();
    VkDeviceSize triangleOffset = supportingHalfedge->getTriangleGeometryOffset();
    size_t triangleCount = supportingHalfedge->getTriangleCount();
    
    if (triangleBuffer == VK_NULL_HANDLE || triangleCount == 0) {
        return;
    }
    
    const auto& descriptorSets = perModelIntrinsicNormalsDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        // Binding 0: IntrinsicTriangleData storage buffer
        VkDescriptorBufferInfo triangleBufferInfo{};
        triangleBufferInfo.buffer = triangleBuffer;
        triangleBufferInfo.offset = triangleOffset;
        triangleBufferInfo.range = triangleCount * sizeof(IntrinsicTriangleData);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &triangleBufferInfo;
        
        // Binding 1: UBO
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void GBuffer::createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Storage buffer (intrinsic vertex data)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicVertexNormalsDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic vertex normals descriptor pool");
    }
}

void GBuffer::createIntrinsicVertexNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Binding 0: IntrinsicVertexData storage buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 1: UBO 
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicVertexNormalsDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic vertex normals descriptor set layout");
    }
}

void GBuffer::allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) != perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicVertexNormalsDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicVertexNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate intrinsic vertex normals descriptor sets");
    }
    
    perModelIntrinsicVertexNormalsDescriptorSets[model] = descriptorSets;    
}

void GBuffer::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    VkBuffer vertexBuffer = supportingHalfedge->getIntrinsicVertexBuffer();
    VkDeviceSize vertexOffset = supportingHalfedge->getVertexGeometryOffset();
    size_t vertexCount = supportingHalfedge->getVertexCount();
    
    if (vertexBuffer == VK_NULL_HANDLE || vertexCount == 0) {
        return;
    }
    
    const auto& descriptorSets = perModelIntrinsicVertexNormalsDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        // Binding 0: IntrinsicVertexData storage buffer
        VkDescriptorBufferInfo vertexBufferInfo{};
        vertexBufferInfo.buffer = vertexBuffer;
        vertexBufferInfo.offset = vertexOffset;
        vertexBufferInfo.range = vertexCount * sizeof(IntrinsicVertexData);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &vertexBufferInfo;
        
        // Binding 1: UBO
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void GBuffer::createGeometryPipeline(VkExtent2D extent) {
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

    // Set vertex binding descriptions
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    // Set vertex attribute descriptions
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.minSampleShading = 1.0f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

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
         VK_DYNAMIC_STATE_DEPTH_BIAS,
         VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline layout with push constant for heatsystem
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GeometryPushConstant);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &geometryDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

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

void GBuffer::createLightingPipeline(VkExtent2D swapchainExtent) {
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
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;  // 8x MSAA lighting output

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

void GBuffer::createOutlinePipeline(VkExtent2D extent) {
    auto vertCode = readFile("shaders/outline_vert.spv");
    auto fragCode = readFile("shaders/outline_frag.spv");

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE; 

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;  
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_TRUE; 
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;  
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;  // Lighting subpass has 1 color attachment
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for outline thickness and color
    VkPushConstantRange outlinePushConstantRange{};
    outlinePushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    outlinePushConstantRange.offset = 0;
    outlinePushConstantRange.size = sizeof(OutlinePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &outlineDescriptorSetLayout; 
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &outlinePushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &outlinePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline layout");
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
    pipelineInfo.layout = outlinePipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 2;  // Grid subpass 

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outlinePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void GBuffer::createSupportingHalfedgePipeline(VkExtent2D extent) {

    auto vertShaderCode = readFile("shaders/intrinsic_supporting_vert.spv"); 
    auto geomShaderCode = readFile("shaders/intrinsic_supporting_geom.spv");  
    auto fragShaderCode = readFile("shaders/intrinsic_supporting_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);  
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;  

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;  // 8x MSAA 

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Match geometry's attachments count and setup
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
         VK_DYNAMIC_STATE_DEPTH_BIAS,
         VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix 
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4); 

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &supportingHalfedgeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &supportingHalfedgePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create supporting halfedge pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3; 
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = supportingHalfedgePipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // geometry subpass 

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &supportingHalfedgePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create supporting halfedge pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);  
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createIntrinsicNormalsPipeline(VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/intrinsic_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_normals_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Point list topology
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
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
    rasterizer.lineWidth = 2.0f;  
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; 
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Match geometry's attachments count
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
         VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix, normal length, and average area
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicNormalsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic normals pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicNormalsPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // geometry subpass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicNormalsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic normals pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createIntrinsicVertexNormalsPipeline(VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/intrinsic_vertex_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_vertex_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_vertex_normals_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Point list topology 
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
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
    rasterizer.lineWidth = 2.0f;  
    rasterizer.cullMode = VK_CULL_MODE_NONE;  
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Match geometry's attachments count
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
         VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix, normal length, and average area
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicVertexNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicVertexNormalsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic vertex normals pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicVertexNormalsPipelineLayout;
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0; // geometry subpass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicVertexNormalsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic vertex normals pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createBlendPipeline(VkExtent2D extent) {
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

void GBuffer::createStencilOnlyPipeline(VkExtent2D extent) {
    // Uses same shaders as geometry pipeline
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

    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.minSampleShading = 1.0f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    // Write stencil only
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;   
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_TRUE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_REPLACE;
    stencilOp.failOp = VK_STENCIL_OP_KEEP;
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
    stencilOp.compareMask = 0xFF;
    stencilOp.writeMask = 0xFF;
    stencilOp.reference = 1;

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    // No color output
    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};
    for (int i = 0; i < 3; ++i) {
        colorBlendAttachments[i].colorWriteMask = 0; 
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR,
         VK_DYNAMIC_STATE_DEPTH_BIAS,
         VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Reuse geometry pipeline layout
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
    pipelineInfo.layout = geometryPipelineLayout;  // Reuse geometry layout
    pipelineInfo.renderPass = deferredRenderer.getRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &stencilOnlyPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create stencil-only pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
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

void GBuffer::recordCommandBuffer(ResourceManager& resourceManager, HeatSystem& heatSystem, 
    ModelSelection& modelSelection, Gizmo& gizmo, WireframeRenderer& wireframeRenderer, std::vector<VkImageView> swapChainImageViews, uint32_t currentFrame, uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent, int wireframeMode, bool drawIntrinsicOverlay, bool drawHeatOverlay, bool drawIntrinsicNormals, bool drawIntrinsicVertexNormals, float normalLength, bool drawHashGrid, bool drawSurfels, bool drawVoronoi, bool drawPoints) {
    VkCommandBuffer commandBuffer = gbufferCommandBuffers[currentFrame];

    // Start recording commands  
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(gbufferCommandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording gbuffer command buffer");
    }

    // Current frame * number of swapchain images + swapchain image index
    size_t framebufferIndex = currentFrame * swapChainImageViews.size() + imageIndex;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = deferredRenderer.getRenderPass();
    renderPassInfo.framebuffer = framebuffers[framebufferIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    VkSubpassBeginInfo subpassBeginInfo{};
    subpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    subpassBeginInfo.contents = VK_SUBPASS_CONTENTS_INLINE;

    VkSubpassBeginInfo nextSubpassBeginInfo{};
    nextSubpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    VkSubpassEndInfo nextSubpassEndInfo{};
    nextSubpassEndInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO;

    std::array<VkClearValue, 13> clearValues{};
    clearValues[0].color = { {clearColorValues[0], clearColorValues[1], clearColorValues[2], 1.0f } };  // Albedo MSAA
    clearValues[1].color = { { 0.0f, 0.0f, 1.0f, 0.0f } };  // Normal MSAA
    clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Position MSAA
    clearValues[3].depthStencil = { 1.0f, 0 };              // Depth MSAA
    clearValues[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Albedo Resolve
    clearValues[5].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Normal Resolve
    clearValues[6].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Position Resolve
    clearValues[7].depthStencil = { 1.0f, 0 };              // Depth Resolve
    clearValues[8].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Lighting MSAA 
    clearValues[9].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Grid MSAA 
    clearValues[10].color = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // Grid Resolve
    clearValues[11].color = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // Lighting Resolve
    clearValues[12].color = { { 0.0f, 0.0f, 0.0f, 1.0f } }; // Swapchain

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass2(commandBuffer, &renderPassInfo, &subpassBeginInfo);

    VkPipeline currentPipeline = (wireframeMode == 1) ? stencilOnlyPipeline : geometryPipeline;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);

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

    {
        // Draw visModel 
        VkBuffer visIndexBuffer = resourceManager.getVisModel().getIndexBuffer();
        VkBuffer visVertexBuffer = resourceManager.getVisModel().getVertexBuffer();
        VkDeviceSize visVertexOffset = resourceManager.getVisModel().getVertexBufferOffset();
        
        // Push constants for visModel
        GeometryPushConstant visPushConstant{};
        visPushConstant.modelMatrix = resourceManager.getVisModel().getModelMatrix();
        visPushConstant.alpha = 1.0f;  
        vkCmdPushConstants(commandBuffer, geometryPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstant), &visPushConstant);
        
        // Set stencil reference for visModel 
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 1);
        
        // Push vismodel furthest away from camera 
        vkCmdSetDepthBias(commandBuffer, 1.0f, 0.0f, 1.0f);
        
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &visVertexBuffer, &visVertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, visIndexBuffer, resourceManager.getVisModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getVisModel().getIndices().size()), 1, 0, 0, 0);
        
        // Reset depth bias
        vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);

        // Draw heat source model
        VkBuffer heatVertexBuffer = resourceManager.getHeatModel().getVertexBuffer();
        VkDeviceSize heatVertexOffset = resourceManager.getHeatModel().getVertexBufferOffset();
        
        // Push constants for heat model
        GeometryPushConstant heatPushConstant{};
        heatPushConstant.modelMatrix = resourceManager.getHeatModel().getModelMatrix();
        heatPushConstant.alpha = 1.0f; 
        vkCmdPushConstants(commandBuffer, geometryPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstant), &heatPushConstant);
        
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 2);
        
        // Heat source model has same bias as vismodel
        vkCmdSetDepthBias(commandBuffer, 1.0f, 0.0f, 1.0f);
        
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &heatVertexBuffer, &heatVertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, resourceManager.getHeatModel().getIndexBuffer(), resourceManager.getHeatModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getHeatModel().getIndices().size()), 1, 0, 0, 0);
        
        // Reset depth bias after heat source
        vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);
    }

    // Draw intrinsic overlay for remeshed models
    if (drawIntrinsicOverlay) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, supportingHalfedgePipeline);
        vkCmdSetDepthBias(commandBuffer, 0.1f, 0.0f, 0.1f);
        
        // Iterate through all remeshed models
        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed) 
                continue;
            
            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) 
                continue;
            
            // Check if this model has descriptor sets allocated
            auto it = perModelSupportingHalfedgeDescriptorSets.find(model);
            if (it == perModelSupportingHalfedgeDescriptorSets.end()) {
                std::cerr << "[GBuffer] Warning: No descriptor sets for remeshed model, skipping" << std::endl;
                continue;
            }
            
            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                std::cerr << "[GBuffer] Warning: Invalid frame index for descriptor sets" << std::endl;
                continue;
            }
            
            // Push model matrix for this model
            glm::mat4 modelMatrix = model->getModelMatrix();
            vkCmdPushConstants(commandBuffer, supportingHalfedgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);
            
            // Bind this model's buffers
            VkBuffer modelVertexBuffer = model->getVertexBuffer();
            VkDeviceSize modelVertexOffset = model->getVertexBufferOffset();
            
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer(), 
                model->getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
            
            // Bind this model's specific descriptor set
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                supportingHalfedgePipelineLayout, 0, 1, 
                &modelDescriptorSets[currentFrame], 0, nullptr);
            
            // Draw this model
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model->getIndices().size()), 1, 0, 0, 0);
        }
        
        vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);
    }

    // Draw intrinsic normals for remeshed models
    if (drawIntrinsicNormals) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicNormalsPipeline);
        
        // Iterate through all remeshed models
        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed) 
                continue;
            
            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) 
                continue;
            
            size_t triangleCount = supportingHalfedge->getTriangleCount();
            if (triangleCount == 0) 
                continue;
            
            auto it = perModelIntrinsicNormalsDescriptorSets.find(model);
            if (it == perModelIntrinsicNormalsDescriptorSets.end()) {
                continue;
            }
            
            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                continue;
            }
            
            float avgArea = 0.0f;
            if (triangleCount > 0) {
                avgArea = supportingHalfedge->getAverageTriangleArea();
            }
            
            NormalPushConstant pushConstants{};
            pushConstants.modelMatrix = model->getModelMatrix();
            pushConstants.normalLength = normalLength;
            pushConstants.avgArea = avgArea;
            
            vkCmdPushConstants(commandBuffer, intrinsicNormalsPipelineLayout, 
                VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
            
            // Bind this model's specific descriptor set
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                intrinsicNormalsPipelineLayout, 0, 1, 
                &modelDescriptorSets[currentFrame], 0, nullptr);
            
            // Draw using point list
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(triangleCount), 1, 0, 0);
        }
    }

    // Draw intrinsic vertex normals for remeshed models
    if (drawIntrinsicVertexNormals) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicVertexNormalsPipeline);
        
        // Iterate through all remeshed models
        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed) 
                continue;
            
            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) 
                continue;
            
            size_t vertexCount = supportingHalfedge->getVertexCount();
            if (vertexCount == 0) 
                continue;
            
            auto it = perModelIntrinsicVertexNormalsDescriptorSets.find(model);
            if (it == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
                continue;
            }
            
            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                continue;
            }
            
            NormalPushConstant pushConstants{};
            pushConstants.modelMatrix = model->getModelMatrix();
            pushConstants.normalLength = normalLength;
            pushConstants.avgArea = 0.0f; // Not used for vertex normals
            
            vkCmdPushConstants(commandBuffer, intrinsicVertexNormalsPipelineLayout, 
                VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
            
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                intrinsicVertexNormalsPipelineLayout, 0, 1, 
                &modelDescriptorSets[currentFrame], 0, nullptr);
            
            // Draw using point list 
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertexCount), 1, 0, 0);
        }
    }

    // Draw heat rendering
    if (drawHeatOverlay && heatSystem.getIsActive()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, heatSystem.getHeatRenderPipeline());
        
        // Draw heat source model with its constant temperature buffer
        Model& heatModel = resourceManager.getHeatModel();
        const auto& heatRenderDescriptorSets = heatSystem.getHeatSource().getHeatRenderDescriptorSets();
        if (!heatRenderDescriptorSets.empty()) {
            // Bind heat source descriptor set
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                heatSystem.getHeatRenderPipelineLayout(), 0, 1, 
                &heatRenderDescriptorSets[currentFrame], 0, nullptr);
            
            // Set model matrix
            glm::mat4 modelMatrix = heatModel.getModelMatrix();
            vkCmdPushConstants(commandBuffer, heatSystem.getHeatRenderPipelineLayout(), 
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(glm::mat4), &modelMatrix);
            
            // Draw heat source model
            VkBuffer modelVertexBuffer = heatModel.getVertexBuffer();
            VkDeviceSize modelVertexOffset = heatModel.getVertexBufferOffset();
            
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
            vkCmdBindIndexBuffer(commandBuffer, heatModel.getIndexBuffer(), 
                heatModel.getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(heatModel.getIndices().size()), 1, 0, 0, 0);
        }
        
        // Draw all receiver models with their computed heat data
        const auto& receivers = heatSystem.getReceivers();
        for (const auto& receiver : receivers) {
            const auto& heatRenderDescriptorSets = receiver->getHeatRenderDescriptorSets();
            if (heatRenderDescriptorSets.empty()) {
                std::cerr << "[GBuffer] No heat render descriptor sets for receiver" << std::endl;
                continue;
            }
            
            // Bind this receiver's heat descriptor set
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                heatSystem.getHeatRenderPipelineLayout(), 0, 1, 
                &heatRenderDescriptorSets[currentFrame], 0, nullptr);
            
            // Set model matrix
            Model& model = receiver->getModel();
            glm::mat4 modelMatrix = model.getModelMatrix();
            vkCmdPushConstants(commandBuffer, heatSystem.getHeatRenderPipelineLayout(), 
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(glm::mat4), &modelMatrix);
            
            // Draw this model with its heat overlay
            VkBuffer modelVertexBuffer = model.getVertexBuffer();
            VkDeviceSize modelVertexOffset = model.getVertexBufferOffset();
            
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
            vkCmdBindIndexBuffer(commandBuffer, model.getIndexBuffer(), 
                model.getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model.getIndices().size()), 1, 0, 0, 0);
        }
    }

    // Transition to lighting subpass
    vkCmdNextSubpass2(commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipelineLayout, 0, 1, &lightingDescriptorSets[currentFrame], 0, nullptr);
    // Draw fullscreen triangle
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    // Transition to grid subpass
    vkCmdNextSubpass2(commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
    
    // Draw screen space outline 
    if (modelSelection.getSelected()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline);
        
        // Bind descriptor set with depth texture
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                               outlinePipelineLayout, 0, 1, &outlineDescriptorSets[currentFrame], 0, nullptr);
        
        // Draw outline for each selected model
        const auto& selectedIDs = modelSelection.getSelectedModelIDsRenderThread();
        for (uint32_t id : selectedIDs) {
            OutlinePushConstant outlinePC;
            outlinePC.outlineThickness = modelSelection.getOutlineThickness();
            outlinePC.selectedModelID = id;
            outlinePC.outlineColor = modelSelection.getOutlineColor();
            
            vkCmdPushConstants(commandBuffer, outlinePipelineLayout, 
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(OutlinePushConstant), &outlinePC);
            
            // Draw fullscreen triangle for this model's outline
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }
    }
    
    // Draw grid
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipelineLayout(), 0, 1, &resourceManager.getGrid().getGridDescriptorSets()[currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, resourceManager.getGrid().vertexCount, 1, 0, 0);
    
    // Draw grid labels
    resourceManager.getGrid().renderLabels(commandBuffer, currentFrame);
    
    // Draw hash grid visualization
    if (drawHashGrid && heatSystem.getIsActive()) {
        heatSystem.renderHashGrids(commandBuffer, currentFrame);
    }
    
    // Draw surfel visualization 
    if (drawSurfels && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderSurfels(commandBuffer, currentFrame, resourceManager.getHeatModel().getModelMatrix(), 0.0025);
    }
    
    // Draw Voronoi surface visualization
    if (drawVoronoi && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderVoronoiSurface(commandBuffer, currentFrame);
    }
    
    // Draw occupancy point visualization
    if (drawPoints && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderOccupancy(commandBuffer, currentFrame, extent);
    }
    
    // wireframeMode: 1 = wire-only, 2 = shaded + wireframe
    if (wireframeMode > 0) {
        wireframeRenderer.bindPipeline(commandBuffer);
        
        // Draw wireframe for visModel
        wireframeRenderer.renderModel(
            commandBuffer,
            resourceManager.getVisModel(),
            geometryDescriptorSets[currentFrame],
            resourceManager.getVisModel().getModelMatrix()
        );
        
        // Draw wireframe for heatModel
        wireframeRenderer.renderModel(
            commandBuffer,
            resourceManager.getHeatModel(),
            geometryDescriptorSets[currentFrame],
            resourceManager.getHeatModel().getModelMatrix()
        );
    }
    
    // Draw gizmo last 
    if (modelSelection.getSelected()) {
        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;  
        clearAttachment.clearValue.depthStencil = {1.0f, 0};  
        
        VkClearRect clearRect{};
        clearRect.rect.offset = {0, 0};
        clearRect.rect.extent = extent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;
        
        vkCmdClearAttachments(commandBuffer, 1, &clearAttachment, 1, &clearRect);
        
        // Calculate gizmo position and scale directly from Gizmo class
        glm::vec3 gizmoPosition = gizmo.calculateGizmoPosition(resourceManager, modelSelection);
        float gizmoScale = gizmo.calculateGizmoScale(resourceManager, modelSelection);
        gizmo.render(commandBuffer, currentFrame, gizmoPosition, extent, gizmoScale);
    }

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

void GBuffer::cleanupFramebuffers(uint32_t maxFramesInFlight) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    for (uint32_t i = 0; i < framebuffers.size(); ++i) {
        if (framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffers[i], nullptr);
            framebuffers[i] = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();
}

void GBuffer::cleanup(uint32_t maxFramesInFlight) {
    vkDestroyPipeline(vulkanDevice.getDevice(), geometryPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), lightingPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), outlinePipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), supportingHalfedgePipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicNormalsPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), blendPipeline, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), stencilOnlyPipeline, nullptr);

    vkDestroyPipelineLayout(vulkanDevice.getDevice(), geometryPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), lightingPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), outlinePipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), supportingHalfedgePipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicNormalsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), blendPipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), lightingDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), geometryDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), blendDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), outlineDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), supportingHalfedgeDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), intrinsicNormalsDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(vulkanDevice.getDevice(), lightingDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), geometryDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), blendDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), outlineDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), supportingHalfedgeDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), intrinsicNormalsDescriptorPool, nullptr);
    
    perModelSupportingHalfedgeDescriptorSets.clear();
    perModelIntrinsicNormalsDescriptorSets.clear();
    
    vkDestroySampler(vulkanDevice.getDevice(), depthSampler, nullptr);
}