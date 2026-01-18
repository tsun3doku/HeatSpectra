#include "SurfelRenderer.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp"
#include "Structs.hpp"
#include "VulkanImage.hpp"
#include "file_utils.h"

#include <array>
#include <cmath>
#include <stdexcept>


SurfelRenderer::SurfelRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager)
    : vulkanDevice(device), memoryAllocator(allocator), uniformBufferManager(uniformBufferManager) {
    params.thermalConductance = 8000.0f;  
    params.contactPressure = 1.0f;        // TODO
    params.frictionCoeff = 0.5f;          // TODO
    params.padding = 0.0f;                 
}

SurfelRenderer::~SurfelRenderer() {
    cleanup();
}

void SurfelRenderer::initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (initialized) 
        return;
    
    createCircleGeometry(16); 
    createSurfelBuffers(maxFramesInFlight);
    createSurfelParamsBuffer();
    createSurfelDescriptorSetLayout();
    createSurfelDescriptorPool(maxFramesInFlight);
    createSurfelDescriptorSets(maxFramesInFlight);
    createSurfelPipeline(renderPass);
    
    initialized = true;
}

void SurfelRenderer::createCircleGeometry(int segments) {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    
    // Center vertex
    vertices.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
    
    // Circle vertices
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        vertices.push_back(glm::vec3(cosf(angle), sinf(angle), 0.0f));
    }
    
    // Create triangular faces
    for (int i = 0; i < segments; i++) {
        indices.push_back(0); // Center
        indices.push_back(1 + i);
        indices.push_back(1 + (i + 1) % segments);
    }
    
    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount = static_cast<uint32_t>(indices.size());
    
    // Create vertex buffer
    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(glm::vec3);
    auto vertexBufferResult = memoryAllocator.allocate(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    vertexBuffer = vertexBufferResult.first;
    vertexBufferOffset = vertexBufferResult.second;
    
    void* vertexData = memoryAllocator.getMappedPointer(vertexBuffer, vertexBufferOffset);
    memcpy(vertexData, vertices.data(), vertexBufferSize);
    
    // Create index buffer
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);
    auto indexBufferResult = memoryAllocator.allocate(
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    indexBuffer = indexBufferResult.first;
    indexBufferOffset = indexBufferResult.second;
    
    void* indexData = memoryAllocator.getMappedPointer(indexBuffer, indexBufferOffset);
    memcpy(indexData, indices.data(), indexBufferSize);
}

void SurfelRenderer::createSurfelBuffers(uint32_t maxFramesInFlight) {
    uniformBuffers.resize(maxFramesInFlight);
    uniformBufferOffsets.resize(maxFramesInFlight);
    mappedUniforms.resize(maxFramesInFlight);
    
    VkDeviceSize uniformBufferSize = sizeof(Surfel);
    
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        auto uniformBufferResult = memoryAllocator.allocate(
            uniformBufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            256  
        );
        uniformBuffers[i] = uniformBufferResult.first;
        uniformBufferOffsets[i] = uniformBufferResult.second;
        
        mappedUniforms[i] = memoryAllocator.getMappedPointer(uniformBuffers[i], uniformBufferOffsets[i]);
    }
}

void SurfelRenderer::createSurfelParamsBuffer() {
    VkDeviceSize bufferSize = sizeof(SurfelParams);
    
    auto bufferResult = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        64 
    );
    
    surfelParamsBuffer = bufferResult.first;
    surfelParamsBufferOffset = bufferResult.second;
    mappedSurfelParamsData = memoryAllocator.getMappedPointer(surfelParamsBuffer, surfelParamsBufferOffset);
    
    // Copy initial params to GPU
    if (mappedSurfelParamsData) {
        memcpy(mappedSurfelParamsData, &params, sizeof(SurfelParams));
    }
}

void SurfelRenderer::createSurfelDescriptorSetLayout() {
    // Binding 0: Surface buffer 
    VkDescriptorSetLayoutBinding surfaceLayoutBinding{};
    surfaceLayoutBinding.binding = 0;
    surfaceLayoutBinding.descriptorCount = 1;
    surfaceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    surfaceLayoutBinding.pImmutableSamplers = nullptr;
    surfaceLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 1: Main UBO
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 1;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 2: Surfel buffer (modelMatrix + surfelRadius)
    VkDescriptorSetLayoutBinding surfelLayoutBinding{};
    surfelLayoutBinding.binding = 2;
    surfelLayoutBinding.descriptorCount = 1;
    surfelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    surfelLayoutBinding.pImmutableSamplers = nullptr;
    surfelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = { surfaceLayoutBinding, uboLayoutBinding, surfelLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surfel descriptor set layout!");
    }
}

void SurfelRenderer::createSurfelDescriptorPool(uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxFramesInFlight * 2);    // UBO + surfel params
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxFramesInFlight);        // Surface buffer
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight);
    
    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surfel descriptor pool!");
    }
}

void SurfelRenderer::createSurfelDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();
    
    descriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate surfel descriptor sets!");
    }

    // Update descriptor sets 
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Binding 0: Surface buffer will be updated per frame
        
        // Binding 1: Main UBO (view/proj matrices)
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);

        // Binding 2: Surfel visualization parameters (modelMatrix + surfelRadius)
        VkDescriptorBufferInfo debugBufferInfo{};
        debugBufferInfo.buffer = uniformBuffers[i];
        debugBufferInfo.offset = uniformBufferOffsets[i];
        debugBufferInfo.range = sizeof(Surfel);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{}; // Only 2 writes (skip binding 0)
        
        // Binding 1: Main UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 1;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        // Binding 2: Debug parameters
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 2;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &debugBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void SurfelRenderer::createSurfelPipeline(VkRenderPass renderPass) {
    auto vertShaderCode = readFile("shaders/surfel_debug_vert.spv");
    auto fragShaderCode = readFile("shaders/surfel_debug_frag.spv");
    
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
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;
    
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
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; 
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surfel pipeline layout!");
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
    pipelineInfo.subpass = 2; // Grid subpass 
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create surfel graphics pipeline!");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void SurfelRenderer::render(VkCommandBuffer cmdBuffer, VkBuffer surfaceBuffer, VkDeviceSize surfaceBufferOffset, uint32_t surfelCount, const Surfel& surfel, uint32_t frameIndex) {
    if (!initialized || frameIndex >= uniformBuffers.size()) 
        return;

    memcpy(mappedUniforms[frameIndex], &surfel, sizeof(Surfel));
    
    // Bind surface buffer for this render call
    VkDescriptorBufferInfo surfaceBufferInfo{};
    surfaceBufferInfo.buffer = surfaceBuffer;
    surfaceBufferInfo.offset = surfaceBufferOffset;
    surfaceBufferInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet surfaceDescriptorWrite{};
    surfaceDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    surfaceDescriptorWrite.dstSet = descriptorSets[frameIndex];
    surfaceDescriptorWrite.dstBinding = 0;
    surfaceDescriptorWrite.dstArrayElement = 0;
    surfaceDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    surfaceDescriptorWrite.descriptorCount = 1;
    surfaceDescriptorWrite.pBufferInfo = &surfaceBufferInfo;
    
    vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &surfaceDescriptorWrite, 0, nullptr);
    
    // Bind pipeline and descriptor sets
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                           0, 1, &descriptorSets[frameIndex], 0, nullptr);
    
    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {vertexBufferOffset};
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, indexBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
    
    // Draw instanced 
    vkCmdDrawIndexed(cmdBuffer, indexCount, surfelCount, 0, 0, 0);
}

void SurfelRenderer::cleanup() {
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
    
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            memoryAllocator.free(uniformBuffers[i], uniformBufferOffsets[i]);
        }
    }
    
    if (surfelParamsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfelParamsBuffer, surfelParamsBufferOffset);
        surfelParamsBuffer = VK_NULL_HANDLE;
    }
    
    if (vertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(vertexBuffer, vertexBufferOffset);
        vertexBuffer = VK_NULL_HANDLE;
    }
    
    if (indexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(indexBuffer, indexBufferOffset);
        indexBuffer = VK_NULL_HANDLE;
    }
    
    uniformBuffers.clear();
    uniformBufferOffsets.clear();
    mappedUniforms.clear();
    descriptorSets.clear();
    
    initialized = false;
}
