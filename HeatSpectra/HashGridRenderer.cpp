#include "HashGridRenderer.hpp"
#include "VulkanDevice.hpp"
#include "UniformBufferManager.hpp"
#include "HashGrid.hpp"
#include "Model.hpp"
#include "VulkanImage.hpp"
#include "file_utils.h"
#include "Structs.hpp"

#include <array>
#include <stdexcept>

HashGridRenderer::HashGridRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager)
    : vulkanDevice(device), uniformBufferManager(uniformBufferManager) {
}

HashGridRenderer::~HashGridRenderer() {
    cleanup();
}

void HashGridRenderer::initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight) {
    if (initialized) 
        return;
        
    createDescriptorSetLayout();
    createDescriptorPool(maxFramesInFlight);
    createPipeline(renderPass, subpass);
    
    initialized = true;
}

void HashGridRenderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    
    // Binding 0: Main UBO 
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 1: Occupied cells buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 2: Hash grid params
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr,
        &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid renderer descriptor set layout");
    }
}

void HashGridRenderer::createDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 2; 
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels * 2;   // UBO + params per model
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;       // Occupied cells per model
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr,
        &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid renderer descriptor pool");
    }
}

void HashGridRenderer::createPipeline(VkRenderPass renderPass, uint32_t subpass) {
    auto vertCode = readFile("shaders/hash_grid_vis_vert.spv");
    auto fragCode = readFile("shaders/hash_grid_vis_frag.spv");

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

    // Line list for wireframe cubes
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
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

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

    if (subpass == 2) {
        // Surface overlay target disabled for hash-grid lines.
        colorBlendAttachments[0].colorWriteMask = 0;
        colorBlendAttachments[0].blendEnable = VK_FALSE;
        colorBlendAttachments[1] = colorBlendAttachments[0];
        colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[1].blendEnable = VK_TRUE;
        colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = (subpass == 2) ? 2 : 1;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix + grid color
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec3);  // Model matrix + grid color
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr,
        &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid renderer pipeline layout");
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hash grid renderer pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void HashGridRenderer::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelDescriptorSets.find(model) != perModelDescriptorSets.end()) {
        return;
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo,
        descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate hash grid renderer descriptor sets for model");
    }
    
    perModelDescriptorSets[model] = descriptorSets;
}

void HashGridRenderer::updateDescriptorSetsForModel(Model* model, HashGrid* hashGrid, uint32_t maxFramesInFlight) {
    if (!model || !hashGrid) 
        return;
    
    // Allocate descriptor sets if not already allocated
    if (perModelDescriptorSets.find(model) == perModelDescriptorSets.end()) {
        allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    const auto& descriptorSets = perModelDescriptorSets[model];
    
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorBufferInfo, 3> bufferInfos{};
        
        // Binding 0: Grid UBO (view/proj matrices)
        bufferInfos[0].buffer = uniformBufferManager.getGridUniformBuffers()[i];
        bufferInfos[0].offset = uniformBufferManager.getGridUniformBufferOffsets()[i];
        bufferInfos[0].range = sizeof(GridUniformBufferObject);
        
        // Binding 1: Occupied cells buffer 
        bufferInfos[1].buffer = hashGrid->getOccupiedCellsBuffer(i);
        bufferInfos[1].offset = hashGrid->getOccupiedCellsBufferOffset(i);
        bufferInfos[1].range = VK_WHOLE_SIZE;
        
        // Binding 2: Hash grid params
        bufferInfos[2].buffer = hashGrid->getParamsBuffer();
        bufferInfos[2].offset = hashGrid->getParamsBufferOffset();
        bufferInfos[2].range = sizeof(HashGrid::HashGridParams);
        
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        for (int j = 0; j < 3; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = descriptorSets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = (j == 1) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 
                                                          : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pBufferInfo = &bufferInfos[j];
        }
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), 
                              descriptorWrites.data(), 0, nullptr);
    }
}

void HashGridRenderer::render(VkCommandBuffer cmdBuffer, Model* model, HashGrid* hashGrid, uint32_t frameIndex, const glm::mat4& modelMatrix, const glm::vec3& color) {
    if (!initialized || !model || !hashGrid) 
        return;
    
    // Check if descriptor sets exist for this model
    auto it = perModelDescriptorSets.find(model);
    if (it == perModelDescriptorSets.end() || frameIndex >= it->second.size()) {
        return;
    }
    
    // Bind pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    // Push model matrix + color
    struct {
        glm::mat4 modelMatrix;
        glm::vec3 color;
    } pushData;
    pushData.modelMatrix = modelMatrix;
    pushData.color = color;
    
    vkCmdPushConstants(cmdBuffer, pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushData), &pushData);
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 0, 1,
                           &it->second[frameIndex], 0, nullptr);
    
    // GPU writes vertex count
    VkBuffer indirectBuffer = hashGrid->getIndirectDrawBuffer(frameIndex);
    VkDeviceSize indirectOffset = hashGrid->getIndirectDrawBufferOffset(frameIndex);
    vkCmdDrawIndirect(cmdBuffer, indirectBuffer, indirectOffset, 1, 0);
}

void HashGridRenderer::cleanup() {
    if (!initialized) 
        return;
    
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
    
    perModelDescriptorSets.clear();
    
    initialized = false;
}
