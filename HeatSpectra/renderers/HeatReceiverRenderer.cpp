#include "HeatReceiverRenderer.hpp"

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/ResourceManager.hpp"
#include "heat/HeatReceiverRuntime.hpp"
#include "scene/Model.hpp"
#include "util/Structs.hpp"
#include "vulkan/VulkanImage.hpp"
#include "util/file_utils.h"

#include <array>
#include <glm/glm.hpp>
#include <iostream>
#include <unordered_set>

HeatReceiverRenderer::HeatReceiverRenderer(VulkanDevice& device, UniformBufferManager& uboManager)
    : vulkanDevice(device), uniformBufferManager(uboManager) {
}

HeatReceiverRenderer::~HeatReceiverRenderer() {
    cleanup();
}

void HeatReceiverRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (initialized) {
        cleanup();
    }

    if (!createDescriptorPool(maxFramesInFlight) ||
        !createDescriptorSetLayout() ||
        !createPipeline(renderPass)) {
        cleanup();
        return;
    }

    initialized = true;
}

bool HeatReceiverRenderer::createDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxRenderableHeatModels = 64;
    const uint32_t totalSets = maxFramesInFlight * maxRenderableHeatModels;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = totalSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = totalSets * 11;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "HeatReceiverRenderer: Failed to create descriptor pool" << std::endl;
        return false;
    }

    return true;
}

bool HeatReceiverRenderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 12> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (int i = 1; i <= 10; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[11].descriptorCount = 1;
    bindings[11].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "HeatReceiverRenderer: Failed to create descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool HeatReceiverRenderer::createPipeline(VkRenderPass renderPass) {
    std::vector<char> vertShaderCode;
    std::vector<char> geomShaderCode;
    std::vector<char> fragShaderCode;
    if (!readFile("shaders/intrinsic_supporting_vert.spv", vertShaderCode) ||
        !readFile("shaders/intrinsic_supporting_geom.spv", geomShaderCode) ||
        !readFile("shaders/heat_buffer_frag.spv", fragShaderCode)) {
        std::cerr << "HeatReceiverRenderer: Failed to read shader files" << std::endl;
        return false;
    }

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule geomShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (createShaderModule(vulkanDevice, vertShaderCode, vertShaderModule) != VK_SUCCESS ||
        createShaderModule(vulkanDevice, geomShaderCode, geomShaderModule) != VK_SUCCESS ||
        createShaderModule(vulkanDevice, fragShaderCode, fragShaderModule) != VK_SUCCESS) {
        if (vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        }
        if (geomShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        }
        if (fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        }
        std::cerr << "HeatReceiverRenderer: Failed to create shader modules" << std::endl;
        return false;
    }

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
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
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

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    colorBlendAttachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(HeatBufferPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        std::cerr << "HeatReceiverRenderer: Failed to create pipeline layout" << std::endl;
        return false;
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 2;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        std::cerr << "HeatReceiverRenderer: Failed to create graphics pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);

    return true;
}

void HeatReceiverRenderer::drawModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, Model& model) const {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    HeatBufferPushConstant pushConstants{};
    pushConstants.modelMatrix = model.getModelMatrix();
    pushConstants.sourceParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(HeatBufferPushConstant),
        &pushConstants);

    VkBuffer modelVertexBuffer = model.getRenderVertexBuffer();
    VkDeviceSize modelVertexOffset = model.getRenderVertexBufferOffset();

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
    vkCmdBindIndexBuffer(commandBuffer, model.getRenderIndexBuffer(), model.getRenderIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model.getRenderIndices().size()), 1, 0, 0, 0);
}

bool HeatReceiverRenderer::updateDescriptorSetVector(const std::array<VkBufferView, 11>& bufferViews, uint32_t maxFramesInFlight,
    std::vector<VkDescriptorSet>& targetSets, bool forceReallocate) {
    if (forceReallocate || targetSets.empty() || targetSets.size() != maxFramesInFlight) {
        targetSets.clear();
        targetSets.resize(maxFramesInFlight);

        std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = maxFramesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, targetSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 12> descriptorWrites{};

        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = targetSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        for (int j = 0; j < 11; j++) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = targetSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].dstArrayElement = 0;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 12, descriptorWrites.data(), 0, nullptr);
    }

    return true;
}

void HeatReceiverRenderer::updateDescriptors(const std::vector<HeatOverlayData>& receivers, uint32_t maxFramesInFlight, bool forceReallocate) {
    if (!initialized) {
        return;
    }

    std::unordered_set<const Model*> liveReceivers;
    liveReceivers.reserve(receivers.size());

    for (const HeatOverlayData& receiver : receivers) {
        if (!receiver.model) {
            continue;
        }

        liveReceivers.insert(receiver.model);
        if (receiver.surfaceBufferView == VK_NULL_HANDLE) {
            receiverDescriptorSets.erase(receiver.model);
            continue;
        }

        auto& receiverSets = receiverDescriptorSets[receiver.model];
        const std::array<VkBufferView, 11> receiverViews = {
            receiver.supportingHalfedgeView,
            receiver.supportingAngleView,
            receiver.halfedgeView,
            receiver.edgeView,
            receiver.triangleView,
            receiver.lengthView,
            receiver.inputHalfedgeView,
            receiver.inputEdgeView,
            receiver.inputTriangleView,
            receiver.inputLengthView,
            receiver.surfaceBufferView
        };

        if (!updateDescriptorSetVector(receiverViews, maxFramesInFlight, receiverSets, forceReallocate)) {
            receiverDescriptorSets.erase(receiver.model);
            std::cerr << "HeatReceiverRenderer: Failed to allocate/update receiver descriptor sets" << std::endl;
            continue;
        }
    }

    for (auto it = receiverDescriptorSets.begin(); it != receiverDescriptorSets.end();) {
        if (liveReceivers.find(it->first) == liveReceivers.end()) {
            it = receiverDescriptorSets.erase(it);
        } else {
            ++it;
        }
    }
}

void HeatReceiverRenderer::updateDescriptors(
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    ResourceManager& resourceManager,
    uint32_t maxFramesInFlight,
    bool forceReallocate) {
    std::vector<HeatOverlayData> overlayReceivers;
    overlayReceivers.reserve(receivers.size());

    for (const auto& receiver : receivers) {
        if (!receiver) {
            continue;
        }

        Model* model = resourceManager.getModelByID(receiver->getRuntimeModelId());
        if (!model) {
            continue;
        }

        HeatOverlayData overlay{};
        overlay.model = model;
        overlay.surfaceBufferView = receiver->getSurfaceBufferView();
        overlay.intrinsicVertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
        overlay.supportingHalfedgeView = receiver->getSupportingHalfedgeView();
        overlay.supportingAngleView = receiver->getSupportingAngleView();
        overlay.halfedgeView = receiver->getHalfedgeView();
        overlay.edgeView = receiver->getEdgeView();
        overlay.triangleView = receiver->getTriangleView();
        overlay.lengthView = receiver->getLengthView();
        overlay.inputHalfedgeView = receiver->getInputHalfedgeView();
        overlay.inputEdgeView = receiver->getInputEdgeView();
        overlay.inputTriangleView = receiver->getInputTriangleView();
        overlay.inputLengthView = receiver->getInputLengthView();
        overlayReceivers.push_back(overlay);
    }

    updateDescriptors(overlayReceivers, maxFramesInFlight, forceReallocate);
}

void HeatReceiverRenderer::render(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<ReceiverRenderBinding>& receivers) const {
    if (!initialized || pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    uint32_t renderedReceiverCount = 0;
    for (const ReceiverRenderBinding& receiverBinding : receivers) {
        if (!receiverBinding.model) {
            continue;
        }

        auto it = receiverDescriptorSets.find(receiverBinding.model);
        if (it == receiverDescriptorSets.end()) {
            continue;
        }
        const auto& receiverHeatSets = it->second;
        if (frameIndex >= receiverHeatSets.size()) {
            continue;
        }

        drawModel(commandBuffer, receiverHeatSets[frameIndex], *receiverBinding.model);
        ++renderedReceiverCount;
    }

    std::cout << "[HeatReceiverRenderer] render"
              << " frame=" << frameIndex
              << " requested=" << receivers.size()
              << " rendered=" << renderedReceiverCount
              << std::endl;
}

void HeatReceiverRenderer::cleanup() {
    VkDevice device = vulkanDevice.getDevice();

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    receiverDescriptorSets.clear();
    initialized = false;
}
