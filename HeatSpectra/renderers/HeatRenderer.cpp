#include "HeatRenderer.hpp"

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "scene/Model.hpp"
#include "heat/HeatReceiver.hpp"
#include "heat/HeatSource.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "mesh/remesher/iODT.hpp"
#include "util/Structs.hpp"
#include "vulkan/VulkanImage.hpp"
#include "util/file_utils.h"

#include <array>
#include <glm/glm.hpp>
#include <iostream>
#include <unordered_set>

HeatRenderer::HeatRenderer(VulkanDevice& device, UniformBufferManager& uboManager)
    : vulkanDevice(device), uniformBufferManager(uboManager) {
}

HeatRenderer::~HeatRenderer() {
    cleanup();
}

void HeatRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
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

bool HeatRenderer::createDescriptorPool(uint32_t maxFramesInFlight) {
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
        std::cerr << "HeatRenderer: Failed to create descriptor pool" << std::endl;
        return false;
    }

    return true;
}

bool HeatRenderer::createDescriptorSetLayout() {
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
        std::cerr << "HeatRenderer: Failed to create descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool HeatRenderer::createPipeline(VkRenderPass renderPass) {
    std::vector<char> vertShaderCode;
    std::vector<char> geomShaderCode;
    std::vector<char> fragShaderCode;
    if (!readFile("shaders/intrinsic_supporting_vert.spv", vertShaderCode) ||
        !readFile("shaders/intrinsic_supporting_geom.spv", geomShaderCode) ||
        !readFile("shaders/heat_buffer_frag.spv", fragShaderCode)) {
        std::cerr << "HeatRenderer: Failed to read shader files" << std::endl;
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
        std::cerr << "HeatRenderer: Failed to create shader modules" << std::endl;
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
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

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
        std::cerr << "HeatRenderer: Failed to create pipeline layout" << std::endl;
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
        std::cerr << "HeatRenderer: Failed to create graphics pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);

    return true;
}

void HeatRenderer::drawModel(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet descriptorSet,
    Model& model) const {
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);

    glm::mat4 modelMatrix = model.getModelMatrix();
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT,
        0,
        sizeof(glm::mat4),
        &modelMatrix);

    VkBuffer modelVertexBuffer = model.getRenderVertexBuffer();
    VkDeviceSize modelVertexOffset = model.getRenderVertexBufferOffset();

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
    vkCmdBindIndexBuffer(
        commandBuffer,
        model.getRenderIndexBuffer(),
        model.getRenderIndexBufferOffset(),
        VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(
        commandBuffer,
        static_cast<uint32_t>(model.getRenderIndices().size()),
        1,
        0,
        0,
        0);
}

bool HeatRenderer::updateDescriptorSetVector(const std::array<VkBufferView, 11>& bufferViews, uint32_t maxFramesInFlight,
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

void HeatRenderer::updateDescriptors(Remesher& remesher, const std::vector<SourceRenderBinding>& sources,
    const std::vector<std::unique_ptr<HeatReceiver>>& receivers, uint32_t maxFramesInFlight, bool forceReallocate) {
    if (!initialized) {
        return;
    }

    std::unordered_set<const Model*> liveSourceModels;
    liveSourceModels.reserve(sources.size());
    for (const SourceRenderBinding& sourceBinding : sources) {
        Model* sourceModel = sourceBinding.model;
        HeatSource* heatSource = sourceBinding.heatSource;
        if (!sourceModel || !heatSource) {
            continue;
        }

        liveSourceModels.insert(sourceModel);
        auto* heatRemesher = remesher.getRemesherForModel(sourceModel);
        if (!heatRemesher) {
            sourceDescriptorSets.erase(sourceModel);
            continue;
        }

        auto* supportingHalfedge = heatRemesher->getSupportingHalfedge();
        if (!supportingHalfedge ||
            !supportingHalfedge->isUploadedToGPU() ||
            heatSource->getSourceBufferView() == VK_NULL_HANDLE ||
            heatSource->getVertexCount() == 0) {
            sourceDescriptorSets.erase(sourceModel);
            continue;
        }

        auto& sourceSets = sourceDescriptorSets[sourceModel];
        const std::array<VkBufferView, 11> heatSourceViews = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView(),
            heatSource->getSourceBufferView()
        };
        if (!updateDescriptorSetVector(heatSourceViews, maxFramesInFlight, sourceSets, forceReallocate)) {
            sourceDescriptorSets.erase(sourceModel);
            std::cerr << "HeatRenderer: Failed to allocate/update source descriptor sets" << std::endl;
            continue;
        }
    }

    for (auto it = sourceDescriptorSets.begin(); it != sourceDescriptorSets.end();) {
        if (liveSourceModels.find(it->first) == liveSourceModels.end()) {
            it = sourceDescriptorSets.erase(it);
        } else {
            ++it;
        }
    }

    std::unordered_set<const HeatReceiver*> liveReceivers;
    liveReceivers.reserve(receivers.size());

    for (const auto& receiver : receivers) {
        liveReceivers.insert(receiver.get());
        Model& model = receiver->getModel();
        auto* modelRemesher = remesher.getRemesherForModel(&model);
        if (!modelRemesher) {
            receiverDescriptorSets.erase(receiver.get());
            continue;
        }
        auto* supportingHalfedge = modelRemesher->getSupportingHalfedge();
        if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU() || receiver->getSurfaceBufferView() == VK_NULL_HANDLE) {
            receiverDescriptorSets.erase(receiver.get());
            continue;
        }

        auto& receiverSets = receiverDescriptorSets[receiver.get()];
        const std::array<VkBufferView, 11> receiverViews = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView(),
            receiver->getSurfaceBufferView()
        };

        if (!updateDescriptorSetVector(receiverViews, maxFramesInFlight, receiverSets, forceReallocate)) {
            receiverDescriptorSets.erase(receiver.get());
            std::cerr << "HeatRenderer: Failed to allocate/update receiver descriptor sets" << std::endl;
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

void HeatRenderer::render(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    const std::vector<SourceRenderBinding>& sources,
    const std::vector<std::unique_ptr<HeatReceiver>>& receivers) const {
    if (!initialized || pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    for (const SourceRenderBinding& sourceBinding : sources) {
        Model* sourceModel = sourceBinding.model;
        if (!sourceModel) {
            continue;
        }

        auto sourceSetIt = sourceDescriptorSets.find(sourceModel);
        if (sourceSetIt == sourceDescriptorSets.end()) {
            continue;
        }

        const auto& sourceSets = sourceSetIt->second;
        if (frameIndex >= sourceSets.size()) {
            continue;
        }

        drawModel(commandBuffer, sourceSets[frameIndex], *sourceModel);
    }

    for (const auto& receiver : receivers) {
        auto it = receiverDescriptorSets.find(receiver.get());
        if (it == receiverDescriptorSets.end()) {
            continue;
        }
        const auto& receiverHeatSets = it->second;
        if (frameIndex >= receiverHeatSets.size()) {
            continue;
        }

        Model& receiverModel = receiver->getModel();
        drawModel(commandBuffer, receiverHeatSets[frameIndex], receiverModel);
    }
}

void HeatRenderer::cleanup() {
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

    sourceDescriptorSets.clear();
    receiverDescriptorSets.clear();
    initialized = false;
}
