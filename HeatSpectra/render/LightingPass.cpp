#include "LightingPass.hpp"

#include <array>
#include <iostream>
#include <vector>

#include "framegraph/FrameGraphPasses.hpp"
#include "util/File_utils.h"
#include "util/Structs.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

namespace render {

LightingPass::LightingPass(
    VulkanDevice& device,
    VkFrameGraphRuntime& runtime,
    UniformBufferManager& ubo,
    uint32_t framesInFlight,
    framegraph::PassId passId,
    framegraph::ResourceId albedoResolveId,
    framegraph::ResourceId normalResolveId,
    framegraph::ResourceId positionResolveId)
    : vulkanDevice(device),
      frameGraphRuntime(runtime),
      uniformBufferManager(ubo),
      maxFramesInFlight(framesInFlight),
      passId(passId),
      albedoResolveId(albedoResolveId),
      normalResolveId(normalResolveId),
      positionResolveId(positionResolveId) {
}

const char* LightingPass::name() const {
    return framegraph::passes::Lighting.data();
}

void LightingPass::create() {
    ready = false;
    destroy();
    if (!createLightingDescriptorPool(maxFramesInFlight)) {
        destroy();
        return;
    }
    if (!createLightingDescriptorSetLayout()) {
        destroy();
        return;
    }
    if (!createLightingDescriptorSets(uniformBufferManager, maxFramesInFlight)) {
        destroy();
        return;
    }
    if (!createLightingPipeline()) {
        destroy();
        return;
    }
    ready = true;
}

void LightingPass::resize(VkExtent2D extent) {
    (void)extent;
}

void LightingPass::updateDescriptors() {
    if (!ready) {
        return;
    }
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = frameGraphRuntime.getResourceViews(albedoResolveId)[i];
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = frameGraphRuntime.getResourceViews(normalResolveId)[i];
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = frameGraphRuntime.getResourceViews(positionResolveId)[i];
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrites[3]{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 3, descriptorWrites, 0, nullptr);
    }
}

void LightingPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) {
    (void)view;
    (void)flags;
    (void)params;
    (void)services;
    if (!ready) {
        return;
    }

    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t frameIndex = context.currentFrame;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipelineLayout, 0, 1, &lightingDescriptorSets[frameIndex], 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void LightingPass::destroy() {
    ready = false;
    if (lightingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), lightingPipeline, nullptr);
        lightingPipeline = VK_NULL_HANDLE;
    }
    if (lightingPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), lightingPipelineLayout, nullptr);
        lightingPipelineLayout = VK_NULL_HANDLE;
    }
    if (lightingDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), lightingDescriptorSetLayout, nullptr);
        lightingDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (lightingDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), lightingDescriptorPool, nullptr);
        lightingDescriptorPool = VK_NULL_HANDLE;
    }
    lightingDescriptorSets.clear();
}

bool LightingPass::createLightingDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 3;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight);

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &lightingDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[LightingPass] Failed to create descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool LightingPass::createLightingDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &lightingDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[LightingPass] Failed to create descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool LightingPass::createLightingDescriptorSets(UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, lightingDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = lightingDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    lightingDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, lightingDescriptorSets.data()) != VK_SUCCESS) {
        std::cerr << "[LightingPass] Failed to allocate descriptor sets" << std::endl;
        return false;
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = frameGraphRuntime.getResourceViews(albedoResolveId)[i];
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = frameGraphRuntime.getResourceViews(normalResolveId)[i];
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = frameGraphRuntime.getResourceViews(positionResolveId)[i];
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = uniformBufferManager.getLightBuffers()[i];
        lightBufferInfo.offset = uniformBufferManager.getLightBufferOffsets()[i];
        lightBufferInfo.range = sizeof(LightUniformBufferObject);

        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        descriptorWrites[3].dstBinding = 4;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &uboBufferInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = lightingDescriptorSets[i];
        descriptorWrites[4].dstBinding = 5;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &lightBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    return true;
}

bool LightingPass::createLightingPipeline() {
    auto vertShaderCode = readFile("shaders/lighting_vert.spv");
    auto fragShaderCode = readFile("shaders/lighting_frag.spv");

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS) {
        std::cerr << "[LightingPass] Failed to create vertex shader module" << std::endl;
        return false;
    }
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        std::cerr << "[LightingPass] Failed to create fragment shader module" << std::endl;
        return false;
    }

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_TRUE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_KEEP;
    stencilOp.failOp = VK_STENCIL_OP_KEEP;
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    stencilOp.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    stencilOp.compareMask = 0xFF;
    stencilOp.writeMask = 0x00;
    stencilOp.reference = 0;
    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
    pipelineLayoutInfo.pSetLayouts = &lightingDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &lightingPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        std::cerr << "[LightingPass] Failed to create pipeline layout" << std::endl;
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
    pipelineInfo.layout = lightingPipelineLayout;
    pipelineInfo.renderPass = frameGraphRuntime.getRenderPass();
    pipelineInfo.subpass = framegraph::toIndex(passId);

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &lightingPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), lightingPipelineLayout, nullptr);
        lightingPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        std::cerr << "[LightingPass] Failed to create graphics pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    return true;
}

} // namespace render


