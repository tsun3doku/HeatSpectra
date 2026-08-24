#include "HeatContactLevelSetRenderer.hpp"

#include "util/Structs.hpp"
#include "util/file_utils.h"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <array>
#include <cstring>
#include <iostream>

HeatContactLevelSetRenderer::HeatContactLevelSetRenderer(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager)
    : vulkanDevice(device),
      memoryAllocator(allocator),
      uniformBufferManager(uniformBufferManager) {
}

HeatContactLevelSetRenderer::~HeatContactLevelSetRenderer() {
    cleanup();
}

void HeatContactLevelSetRenderer::initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight) {
    if (initialized) {
        cleanup();
    }

    if (!createUniformBuffers(maxFramesInFlight) ||
        !createDescriptorSetLayout() ||
        !createDescriptorPool(maxFramesInFlight) ||
        !createPipeline(renderPass, subpass)) {
        cleanup();
        return;
    }

    initialized = true;
}

bool HeatContactLevelSetRenderer::createUniformBuffers(uint32_t frames) {
    uboBuffers.resize(frames, VK_NULL_HANDLE);
    uboBufferOffsets.resize(frames, 0);
    uboMappedPtrs.resize(frames, nullptr);

    for (uint32_t i = 0; i < frames; ++i) {
        if (createUniformBuffer(memoryAllocator, vulkanDevice, sizeof(heat::ContactCameraUbo),
                                uboBuffers[i], uboBufferOffsets[i], &uboMappedPtrs[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool HeatContactLevelSetRenderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxSdfTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxPsiTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        0, 0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &flagsInfo };
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    return vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS;
}

bool HeatContactLevelSetRenderer::createDescriptorPool(uint32_t frames) {
    uint32_t totalSets = frames * MaxBindingsPerFrame;
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSets };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, totalSets };
    poolSizes[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalSets * MaxSdfTextures };
    poolSizes[3] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalSets * MaxPsiTextures };

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    descriptorSets.resize(frames);

    std::vector<VkDescriptorSetLayout> layouts(MaxBindingsPerFrame, descriptorSetLayout);
    for (uint32_t f = 0; f < frames; ++f) {
        descriptorSets[f].resize(MaxBindingsPerFrame, VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = MaxBindingsPerFrame;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets[f].data()) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool HeatContactLevelSetRenderer::createPipeline(VkRenderPass renderPass, uint32_t subpass) {
    std::vector<char> vertShaderCode, fragShaderCode;
    if (!readFile("shaders/heat_contact_levelset_vert.spv", vertShaderCode) ||
        !readFile("shaders/heat_contact_levelset_frag.spv", fragShaderCode)) {
        return false;
    }

    VkShaderModuleCreateInfo vertInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vertShaderCode.size(), reinterpret_cast<const uint32_t*>(vertShaderCode.data()) };
    VkShaderModuleCreateInfo fragInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, fragShaderCode.size(), reinterpret_cast<const uint32_t*>(fragShaderCode.data()) };

    VkShaderModule vertShaderModule = VK_NULL_HANDLE, fragShaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkanDevice.getDevice(), &vertInfo, nullptr, &vertShaderModule) != VK_SUCCESS ||
        vkCreateShaderModule(vulkanDevice.getDevice(), &fragInfo, nullptr, &fragShaderModule) != VK_SUCCESS) {
        if (vertShaderModule) vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        if (fragShaderModule) vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr };
    shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr };

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };

    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &colorBlendAttachment };

    std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data() };

    VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(heat::ContactLevelSetPushConstant) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &descriptorSetLayout, 1, &pushConstantRange };

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;

    VkResult res = vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    return res == VK_SUCCESS;
}

void HeatContactLevelSetRenderer::render(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    const std::vector<RenderBinding>& bindings) {
    if (!initialized || pipeline == VK_NULL_HANDLE || bindings.empty() || frameIndex >= descriptorSets.size()) {
        return;
    }

    const auto& uboMappedList = uniformBufferManager.getUniformBuffersMapped();
    const UniformBufferObject* mainUbo = frameIndex < uboMappedList.size()
        ? static_cast<const UniformBufferObject*>(uboMappedList[frameIndex]) : nullptr;

    heat::ContactCameraUbo cameraUbo{};
    if (mainUbo) {
        cameraUbo.view = mainUbo->view;
        cameraUbo.proj = mainUbo->proj;
        cameraUbo.invView = glm::inverse(mainUbo->view);
    }
    if (frameIndex < uboMappedPtrs.size() && uboMappedPtrs[frameIndex]) {
        std::memcpy(uboMappedPtrs[frameIndex], &cameraUbo, sizeof(heat::ContactCameraUbo));
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    uint32_t activeBindingCount = std::min(static_cast<uint32_t>(bindings.size()), MaxBindingsPerFrame);
    for (uint32_t b = 0; b < activeBindingCount; ++b) {
        const auto& binding = bindings[b];
        if (!binding.regionBuffer || !binding.indirectDrawBuffer || binding.sdfImageViews.empty() ||
            !binding.sdfSampler || !binding.regionBufferSize || !(binding.cellSize > 0.0f) ||
            binding.gridDim.x < 2 || binding.gridDim.y < 2 || binding.gridDim.z < 2 ||
            binding.range < 0.0f || binding.sdfImageViews.size() > MaxSdfTextures ||
            binding.psiImageViews.size() > MaxPsiTextures) {
            continue;
        }

        VkDescriptorSet descSet = descriptorSets[frameIndex][b];

        VkDescriptorBufferInfo uboInfo{ uboBuffers[frameIndex], uboBufferOffsets[frameIndex], sizeof(heat::ContactCameraUbo) };
        VkDescriptorBufferInfo regionInfo{ binding.regionBuffer, binding.regionBufferOffset, binding.regionBufferSize };

        uint32_t imageCount = static_cast<uint32_t>(binding.sdfImageViews.size());
        std::vector<VkDescriptorImageInfo> imageInfos(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            imageInfos[i] = { binding.sdfSampler, binding.sdfImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }

        uint32_t psiCount = static_cast<uint32_t>(binding.psiImageViews.size());
        std::vector<VkDescriptorImageInfo> psiInfos(psiCount);
        for (uint32_t i = 0; i < psiCount; ++i) {
            psiInfos[i] = { binding.sdfSampler, binding.psiImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }

        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr };
        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &regionInfo, nullptr };
        writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 2, 0, imageCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageInfos.data(), nullptr, nullptr };
        writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 3, 0, psiCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, psiInfos.data(), nullptr, nullptr };

        uint32_t writeCount = psiCount > 0 ? 4 : 3;
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), writeCount, writes.data(), 0, nullptr);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet, 0, nullptr);

        heat::ContactLevelSetPushConstant pc{ binding.gridMin, binding.cellSize, binding.gridDim, binding.range };
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDrawIndirect(commandBuffer, binding.indirectDrawBuffer, binding.indirectDrawOffset, 1, sizeof(VkDrawIndirectCommand));
    }
}

void HeatContactLevelSetRenderer::cleanup() {
    initialized = false;

    if (pipeline) { vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (pipelineLayout) { vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    if (descriptorPool) { vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr); descriptorPool = VK_NULL_HANDLE; }
    descriptorSets.clear();

    if (descriptorSetLayout) { vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr); descriptorSetLayout = VK_NULL_HANDLE; }

    for (size_t i = 0; i < uboBuffers.size(); ++i) {
        freeBuffer(memoryAllocator, uboBuffers[i], uboBufferOffsets[i]);
    }
    uboBuffers.clear();
    uboBufferOffsets.clear();
    uboMappedPtrs.clear();
}