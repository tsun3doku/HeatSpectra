#include "VoronoiRenderer.hpp"
#include "VulkanDevice.hpp"
#include "UniformBufferManager.hpp"
#include "Model.hpp"
#include "Structs.hpp"
#include "VulkanImage.hpp"
#include "file_utils.h"

#include <stdexcept>
#include <array>

VoronoiRenderer::VoronoiRenderer(VulkanDevice& device, UniformBufferManager& uboManager)
    : vulkanDevice(device), uniformBufferManager(uboManager) {
}

VoronoiRenderer::~VoronoiRenderer() {
    cleanup();
}

void VoronoiRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (initialized) {
        cleanup();
    }
    
    createDescriptorSetLayout();
    createDescriptorPool(maxFramesInFlight);
    createDescriptorSets(maxFramesInFlight);
    createPipeline(renderPass);
    
    initialized = true;
}

void VoronoiRenderer::createDescriptorSetLayout() {
    // Binding 0: UBO 
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 2: Seed positions 
    VkDescriptorSetLayoutBinding seedLayoutBinding{};
    seedLayoutBinding.binding = 2;
    seedLayoutBinding.descriptorCount = 1;
    seedLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    seedLayoutBinding.pImmutableSamplers = nullptr;
    seedLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 4: Voronoi neighbors 
    VkDescriptorSetLayoutBinding neighborLayoutBinding{};
    neighborLayoutBinding.binding = 4;
    neighborLayoutBinding.descriptorCount = 1;
    neighborLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    neighborLayoutBinding.pImmutableSamplers = nullptr;
    neighborLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding candidateLayoutBinding{};
    candidateLayoutBinding.binding = 12;
    candidateLayoutBinding.descriptorCount = 1;
    candidateLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    candidateLayoutBinding.pImmutableSamplers = nullptr;
    candidateLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Supporting halfedge buffers for intrinsic walk
    VkDescriptorSetLayoutBinding supportingLayoutBinding{};
    supportingLayoutBinding.binding = 6;
    supportingLayoutBinding.descriptorCount = 1;
    supportingLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    supportingLayoutBinding.pImmutableSamplers = nullptr;
    supportingLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding supportingAngleLayoutBinding{};
    supportingAngleLayoutBinding.binding = 7;
    supportingAngleLayoutBinding.descriptorCount = 1;
    supportingAngleLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    supportingAngleLayoutBinding.pImmutableSamplers = nullptr;
    supportingAngleLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding halfedgeLayoutBinding{};
    halfedgeLayoutBinding.binding = 8;
    halfedgeLayoutBinding.descriptorCount = 1;
    halfedgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    halfedgeLayoutBinding.pImmutableSamplers = nullptr;
    halfedgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding edgeLayoutBinding{};
    edgeLayoutBinding.binding = 9;
    edgeLayoutBinding.descriptorCount = 1;
    edgeLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    edgeLayoutBinding.pImmutableSamplers = nullptr;
    edgeLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding triLayoutBinding{};
    triLayoutBinding.binding = 10;
    triLayoutBinding.descriptorCount = 1;
    triLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    triLayoutBinding.pImmutableSamplers = nullptr;
    triLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lengthLayoutBinding{};
    lengthLayoutBinding.binding = 11;
    lengthLayoutBinding.descriptorCount = 1;
    lengthLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    lengthLayoutBinding.pImmutableSamplers = nullptr;
    lengthLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 10> bindings = { 
        uboLayoutBinding, seedLayoutBinding, neighborLayoutBinding,
        supportingLayoutBinding, supportingAngleLayoutBinding, halfedgeLayoutBinding, edgeLayoutBinding, triLayoutBinding, lengthLayoutBinding,
        candidateLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi renderer descriptor set layout!");
    }
}

void VoronoiRenderer::createDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * 3; 
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[2].descriptorCount = maxFramesInFlight * 6;


    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi renderer descriptor pool!");
    }
}

void VoronoiRenderer::createDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Voronoi renderer descriptor sets!");
    }
}

void VoronoiRenderer::updateDescriptors(uint32_t frameIndex,
    uint32_t vertexCount, VkBuffer seedBuffer, VkDeviceSize seedOffset,
    VkBuffer neighborBuffer, VkDeviceSize neighborOffset,
    VkBufferView supportingHalfedgeView, VkBufferView supportingAngleView,
    VkBufferView halfedgeView, VkBufferView edgeView,
    VkBufferView triangleView, VkBufferView lengthView,
    VkBuffer candidateBuffer, VkDeviceSize candidateOffset) {
    currentVertexCount = vertexCount;
    currentCandidateBuffer = candidateBuffer;
    
    if (frameIndex >= descriptorSets.size()) {
        return;
    }

    std::array<VkWriteDescriptorSet, 10> descriptorWrites{};

    // Binding 0: UBO
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBufferManager.getUniformBuffers()[frameIndex];
    uboInfo.offset = uniformBufferManager.getUniformBufferOffsets()[frameIndex];
    uboInfo.range = sizeof(UniformBufferObject);
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[frameIndex];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &uboInfo;

    // Binding 2: Seed positions
    VkDescriptorBufferInfo seedInfo{};
    seedInfo.buffer = seedBuffer;
    seedInfo.offset = seedOffset;
    seedInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[frameIndex];
    descriptorWrites[1].dstBinding = 2;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &seedInfo;
    
    // Binding 4: Voronoi neighbors
    VkDescriptorBufferInfo neighborInfo{};
    neighborInfo.buffer = neighborBuffer;
    neighborInfo.offset = neighborOffset;
    neighborInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSets[frameIndex];
    descriptorWrites[2].dstBinding = 4;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &neighborInfo;

    VkDescriptorBufferInfo candidateInfo{};
    candidateInfo.buffer = candidateBuffer;
    candidateInfo.offset = candidateOffset;
    candidateInfo.range = VK_WHOLE_SIZE;

    VkBufferView supportingViews[6] = {
        supportingHalfedgeView,
        supportingAngleView,
        halfedgeView,
        edgeView,
        triangleView,
        lengthView
    };

    for (int i = 0; i < 6; ++i) {
        descriptorWrites[3 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3 + i].dstSet = descriptorSets[frameIndex];
        descriptorWrites[3 + i].dstBinding = 6 + i;
        descriptorWrites[3 + i].dstArrayElement = 0;
        descriptorWrites[3 + i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        descriptorWrites[3 + i].descriptorCount = 1;
        descriptorWrites[3 + i].pTexelBufferView = &supportingViews[i];
    }

    descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[9].dstSet = descriptorSets[frameIndex];
    descriptorWrites[9].dstBinding = 12;
    descriptorWrites[9].dstArrayElement = 0;
    descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[9].descriptorCount = 1;
    descriptorWrites[9].pBufferInfo = &candidateInfo;

    vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VoronoiRenderer::createPipeline(VkRenderPass renderPass) {
    auto vertShaderCode = readFile("shaders/voronoi_surface_vert.spv");
    auto geomShaderCode = readFile("shaders/voronoi_surface_geom.spv");
    auto fragShaderCode = readFile("shaders/voronoi_surface_frag.spv");
    
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
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input from Model class (extrinsic mesh)
    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto attributeDescriptions = Vertex::getVertexAttributes();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.minSampleShading = 0.25f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target.
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target disabled for Voronoi fill.
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GeometryPushConstant);  
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi renderer pipeline layout!");
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
    pipelineInfo.subpass = 2; // Grid subpass
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Voronoi renderer pipeline!");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
}

void VoronoiRenderer::render(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
    VkBuffer indexBuffer, VkDeviceSize indexOffset, uint32_t indexCount, 
    uint32_t frameIndex, const glm::mat4& modelMatrix) {
    if (!initialized || currentCandidateBuffer == VK_NULL_HANDLE) {
        return;
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, 
                           &descriptorSets[frameIndex], 0, nullptr);
    
    GeometryPushConstant pushConstant{};
    pushConstant.modelMatrix = modelMatrix;
    pushConstant.alpha = 1.0f; 
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                      0, sizeof(GeometryPushConstant), &pushConstant);
    
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {vertexOffset};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, indexOffset, VK_INDEX_TYPE_UINT32);
    
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void VoronoiRenderer::cleanup() {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    descriptorSets.clear();
    initialized = false;
}
