#include "PickPass.hpp"

#include <array>
#include <iostream>
#include <vector>

#include "GeometryPass.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "renderers/GizmoRenderer.hpp"
#include "scene/GizmoController.hpp"
#include "scene/Model.hpp"
#include "scene/ModelSelection.hpp"
#include "scene/PickId.hpp"
#include "util/file_utils.h"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

namespace render {

PickPass::PickPass(
    VulkanDevice& device,
    VkFrameGraphRuntime& runtime,
    ModelRegistry& resources,
    GeometryPass& geometry,
    GizmoRenderer& gizmo,
    framegraph::PassId pickPassId)
    : vulkanDevice(device),
      frameGraphRuntime(runtime),
      modelRegistry(resources),
      geometryPass(geometry),
      gizmoRenderer(gizmo),
      passId(pickPassId) {
}

const char* PickPass::name() const {
    return framegraph::passes::Pick.data();
}

void PickPass::create() {
    ready = false;
    destroy();
    if (!createModelPipeline()) {
        destroy();
        return;
    }
    if (!gizmoRenderer.createPickPipeline(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId))) {
        destroy();
        return;
    }
    ready = true;
}

void PickPass::resize(VkExtent2D extent) {
    (void)extent;
}

void PickPass::updateDescriptors() {
}

void PickPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) {
    (void)view;
    (void)flags;
    if (!ready || !services.modelSelection || !services.gizmoController) {
        return;
    }

    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t frameIndex = context.currentFrame;
    VkDescriptorSet geometryDescriptorSet = geometryPass.getDescriptorSet(frameIndex);
    if (geometryDescriptorSet == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout, 0, 1, &geometryDescriptorSet, 0, nullptr);

    for (uint32_t modelId : modelRegistry.getRenderableModelIds()) {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferOffset = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexBufferOffset = 0;
        uint32_t indexCount = 0;
        glm::mat4 modelMatrix{1.0f};
        if (!modelRegistry.tryGetRenderGeometry(
                modelId, vertexBuffer, vertexBufferOffset,
                indexBuffer, indexBufferOffset, indexCount) ||
            !modelRegistry.tryGetModelMatrix(modelId, modelMatrix)) {
            continue;
        }

        PickPushConstant pushConstant{};
        pushConstant.modelMatrix = modelMatrix;
        pushConstant.pickId = pickid::encodeModel(modelId);
        vkCmdPushConstants(commandBuffer, modelPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PickPushConstant), &pushConstant);

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexBufferOffset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
    }

    ModelSelection& modelSelection = *services.modelSelection;
    GizmoController& gizmoController = *services.gizmoController;
    if (modelSelection.getSelected()) {
        const glm::vec3 gizmoPosition = gizmoController.calculateGizmoPosition(modelRegistry, modelSelection);
        const float gizmoScale = gizmoRenderer.calculateGizmoScale(modelRegistry, modelSelection);
        gizmoRenderer.renderPick(commandBuffer, gizmoPosition, context.extent, gizmoScale, view, gizmoController);
    }
}

void PickPass::destroy() {
    ready = false;
    if (modelPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), modelPipeline, nullptr);
        modelPipeline = VK_NULL_HANDLE;
    }
    if (modelPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), modelPipelineLayout, nullptr);
        modelPipelineLayout = VK_NULL_HANDLE;
    }
    gizmoRenderer.destroyPickPipeline();
}

bool PickPass::createModelPipeline() {
    auto vertShaderCode = readFile("shaders/pick_model_vert.spv");
    auto fragShaderCode = readFile("shaders/pick_model_frag.spv");

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS) {
        std::cerr << "[PickPass] Failed to create model pick vertex shader module" << std::endl;
        return false;
    }

    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        std::cerr << "[PickPass] Failed to create model pick fragment shader module" << std::endl;
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInput.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof(PickPushConstant);

    VkDescriptorSetLayout descriptorSetLayout = geometryPass.getDescriptorSetLayout();
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &modelPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        std::cerr << "[PickPass] Failed to create model pick pipeline layout" << std::endl;
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = modelPipelineLayout;
    pipelineInfo.renderPass = frameGraphRuntime.getRenderPass();
    pipelineInfo.subpass = framegraph::toIndex(passId);

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &modelPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), modelPipelineLayout, nullptr);
        modelPipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        std::cerr << "[PickPass] Failed to create model pick pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    return true;
}

}
