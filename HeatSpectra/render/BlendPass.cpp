#include "BlendPass.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "vulkan/CommandBufferManager.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "util/File_utils.h"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"
#include "libs/stb/stb_image.h"

namespace {

std::string resolveBackgroundPath() {
    const std::array<const char*, 6> candidates = {
        "textures/background.png",
        "HeatSpectra/textures/background.png",
        "../textures/background.png",
        "../../textures/background.png",
        "../HeatSpectra/textures/background.png",
        "../../HeatSpectra/textures/background.png"
    };

    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

}

namespace render {

BlendPass::BlendPass(VulkanDevice& device, CommandPool& commandPool, VkFrameGraphRuntime& runtime, uint32_t framesInFlight, framegraph::PassId passId,
    framegraph::ResourceId surfaceResolveId, framegraph::ResourceId lineResolveId, framegraph::ResourceId lightingResolveId, framegraph::ResourceId albedoResolveId)
    : vulkanDevice(device),
      commandPool(commandPool),
      frameGraphRuntime(runtime),
      maxFramesInFlight(framesInFlight),
      passId(passId),
      surfaceResolveId(surfaceResolveId),
      lineResolveId(lineResolveId),
      lightingResolveId(lightingResolveId),
      albedoResolveId(albedoResolveId) {
}

const char* BlendPass::name() const {
    return framegraph::passes::Blend.data();
}

void BlendPass::create() {
    ready = false;
    destroy();

    if (!createBackgroundResources()) {
        destroy();
        return;
    }
    if (!createBlendDescriptorPool(maxFramesInFlight)) {
        destroy();
        return;
    }
    if (!createBlendDescriptorSetLayout()) {
        destroy();
        return;
    }
    if (!createBlendDescriptorSets(maxFramesInFlight)) {
        destroy();
        return;
    }
    if (!createBlendPipeline()) {
        destroy();
        return;
    }

    ready = true;
}

void BlendPass::resize(VkExtent2D extent) {
    (void)extent;
}

void BlendPass::updateDescriptors() {
    if (!ready) {
        return;
    }
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        VkDescriptorImageInfo imageInfos[5]{};
        imageInfos[0].imageView = frameGraphRuntime.getResourceViews(surfaceResolveId)[i];
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = frameGraphRuntime.getResourceViews(lineResolveId)[i];
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = frameGraphRuntime.getResourceViews(lightingResolveId)[i];
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = frameGraphRuntime.getResourceViews(albedoResolveId)[i];
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[4].imageView = backgroundImageView;
        imageInfos[4].sampler = backgroundSampler;
        imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrites[5]{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = blendDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = blendDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = blendDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &imageInfos[2];

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = blendDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &imageInfos[3];

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = blendDescriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &imageInfos[4];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 5, descriptorWrites, 0, nullptr);
    }
}

void BlendPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) {
    (void)view;
    (void)flags;
    (void)params;
    (void)services;
    if (!ready) {
        return;
    }

    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t frameIndex = context.currentFrame;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipelineLayout, 0, 1, &blendDescriptorSets[frameIndex], 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void BlendPass::destroy() {
    ready = false;
    if (blendPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), blendPipeline, nullptr);
        blendPipeline = VK_NULL_HANDLE;
    }
    if (blendPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), blendPipelineLayout, nullptr);
        blendPipelineLayout = VK_NULL_HANDLE;
    }
    if (blendDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), blendDescriptorSetLayout, nullptr);
        blendDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (blendDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), blendDescriptorPool, nullptr);
        blendDescriptorPool = VK_NULL_HANDLE;
    }
    destroyBackgroundResources();
    blendDescriptorSets.clear();
}

bool BlendPass::createBlendDescriptorPool(uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[0].descriptorCount = maxFramesInFlight * 4;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = maxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &blendDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to create descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool BlendPass::createBlendDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[5] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &blendDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to create descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool BlendPass::createBlendDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, blendDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = blendDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    blendDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, blendDescriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to allocate descriptor sets" << std::endl;
        return false;
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorImageInfo imageInfos[5] = {};
        imageInfos[0].imageView = frameGraphRuntime.getResourceViews(surfaceResolveId)[i];
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = frameGraphRuntime.getResourceViews(lineResolveId)[i];
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = frameGraphRuntime.getResourceViews(lightingResolveId)[i];
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = frameGraphRuntime.getResourceViews(albedoResolveId)[i];
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[4].imageView = backgroundImageView;
        imageInfos[4].sampler = backgroundSampler;
        imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrites[5] = {};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = blendDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = blendDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = blendDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &imageInfos[2];

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = blendDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &imageInfos[3];

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = blendDescriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &imageInfos[4];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 5, descriptorWrites, 0, nullptr);
    }
    return true;
}

bool BlendPass::createBlendPipeline() {
    auto vertCode = readFile("shaders/blend_vert.spv");
    auto fragCode = readFile("shaders/blend_frag.spv");

    VkShaderModule vertModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertCode, vertModule) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to create vertex shader module" << std::endl;
        return false;
    }
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragCode, fragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
        std::cerr << "[BlendPass] Failed to create fragment shader module" << std::endl;
        return false;
    }

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

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
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
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
        std::cerr << "[BlendPass] Failed to create pipeline layout" << std::endl;
        return false;
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
    pipelineInfo.renderPass = frameGraphRuntime.getRenderPass();
    pipelineInfo.subpass = framegraph::toIndex(passId);

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &blendPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), blendPipelineLayout, nullptr);
        blendPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
        std::cerr << "[BlendPass] Failed to create graphics pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
    return true;
}

bool BlendPass::createBackgroundResources() {
    const std::string path = resolveBackgroundPath();
    if (path.empty()) {
        std::cerr << "[BlendPass] Missing textures/background.png" << std::endl;
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        std::cerr << "[BlendPass] Failed to load background texture image" << std::endl;
        return false;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    if (vulkanDevice.createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory,
        stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        std::cerr << "[BlendPass] Failed to create staging buffer" << std::endl;
        return false;
    }

    void* mappedData = nullptr;
    if (vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &mappedData) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        stbi_image_free(pixels);
        std::cerr << "[BlendPass] Failed to map staging memory" << std::endl;
        return false;
    }
    memcpy(mappedData, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);
    stbi_image_free(pixels);

    if (createImage(
        vulkanDevice,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        backgroundImage,
        backgroundImageMemory,
        VK_SAMPLE_COUNT_1_BIT) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        std::cerr << "[BlendPass] Failed to create background image" << std::endl;
        return false;
    }

    if (transitionImageLayout(
        commandPool,
        backgroundImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        std::cerr << "[BlendPass] Failed to transition image to transfer-dst" << std::endl;
        return false;
    }

    commandPool.copyBufferToImage(
        stagingBuffer,
        backgroundImage,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));

    if (transitionImageLayout(
        commandPool,
        backgroundImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        std::cerr << "[BlendPass] Failed to transition image to shader-read" << std::endl;
        return false;
    }

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);

    if (createImageView(
        vulkanDevice,
        backgroundImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_ASPECT_COLOR_BIT,
        backgroundImageView) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to create background image view" << std::endl;
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &backgroundSampler) != VK_SUCCESS) {
        std::cerr << "[BlendPass] Failed to create background sampler" << std::endl;
        return false;
    }
    return true;
}

void BlendPass::destroyBackgroundResources() {
    if (backgroundSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice.getDevice(), backgroundSampler, nullptr);
        backgroundSampler = VK_NULL_HANDLE;
    }
    if (backgroundImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice.getDevice(), backgroundImageView, nullptr);
        backgroundImageView = VK_NULL_HANDLE;
    }
    if (backgroundImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice.getDevice(), backgroundImage, nullptr);
        backgroundImage = VK_NULL_HANDLE;
    }
    if (backgroundImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), backgroundImageMemory, nullptr);
        backgroundImageMemory = VK_NULL_HANDLE;
    }
}

} // namespace render
