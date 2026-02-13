#include "GridLabel.hpp"
#include "VulkanDevice.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp"
#include "file_utils.h"
#include "Structs.hpp"
#include <array>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <fstream>
#include "libs/stb/stb_image.h"

GridLabel::GridLabel(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,
                     UniformBufferManager& uniformBufferManager,
                     uint32_t maxFramesInFlight, VkRenderPass renderPass, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), uniformBufferManager(uniformBufferManager), commandPool(commandPool) {
    
    initializeCharMap();
    createQuadVertexBuffer(vulkanDevice);
    createInstanceBuffer(vulkanDevice, maxFramesInFlight);
    createFontAtlas(vulkanDevice);
    createDescriptorPool(vulkanDevice, maxFramesInFlight);
    createDescriptorSetLayout(vulkanDevice);
    createDescriptorSets(vulkanDevice, uniformBufferManager, maxFramesInFlight);
    createPipeline(vulkanDevice, renderPass);
}

GridLabel::~GridLabel() {
}

void GridLabel::createQuadVertexBuffer(VulkanDevice& vulkanDevice) {
    // Create a quad
    std::vector<QuadVertex> vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},  // Bottom left
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},  // Bottom right
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},  // Top right
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}}   // Top left
    };

    VkDeviceSize bufferSize = sizeof(QuadVertex) * vertices.size();

    // Create vertex buffer 
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &quadVertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create quad vertex buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), quadVertexBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, 
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &quadVertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate quad vertex buffer memory");
    }
    
    vkBindBufferMemory(vulkanDevice.getDevice(), quadVertexBuffer, quadVertexBufferMemory, 0);
    
    // Upload data
    void* data;
    vkMapMemory(vulkanDevice.getDevice(), quadVertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(vulkanDevice.getDevice(), quadVertexBufferMemory);
}

void GridLabel::createInstanceBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    VkDeviceSize bufferSize = sizeof(LabelInstance) * 1000;

    instanceBuffers.resize(maxFramesInFlight);
    instanceBufferMemories.resize(maxFramesInFlight);
    instanceBuffersMapped.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Create buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &instanceBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create instance buffer");
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), instanceBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits,
                                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &instanceBufferMemories[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate instance buffer memory");
        }

        vkBindBufferMemory(vulkanDevice.getDevice(), instanceBuffers[i], instanceBufferMemories[i], 0);

        vkMapMemory(vulkanDevice.getDevice(), instanceBufferMemories[i], 0, bufferSize, 0, 
                    &instanceBuffersMapped[i]);
    }
}

void GridLabel::createFontAtlas(VulkanDevice& vulkanDevice) {
    int texWidth, texHeight;
    stbi_uc* pixels = stbi_load("textures/Roboto-Medium.png", &texWidth, &texHeight, nullptr, STBI_rgb_alpha);
    
    if (!pixels) {
        throw std::runtime_error("Failed to load SDF font atlas texture");
    }
    
    // Calculate Mip Levels
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    VkDeviceSize imageSize = texWidth * texHeight * 4;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer memory");
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingBufferMemory, 0);

    void* data;
    vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);
    
    stbi_image_free(pixels);

    // Create Image with mip levels
    createImage(vulkanDevice, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fontAtlasImage, fontAtlasMemory,
                VK_SAMPLE_COUNT_1_BIT, mipLevels);

    // Transition image to TRANSFER_DST_OPTIMAL for copying
    transitionImageLayout(commandPool, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
    
    commandPool.copyBufferToImage(stagingBuffer, fontAtlasImage, texWidth, texHeight);
    
    generateMipmaps(fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, mipLevels);
    
    fontAtlasView = createImageView(vulkanDevice, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    // Enable Anisotropy
    samplerInfo.anisotropyEnable = VK_TRUE;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    
    // Limit maxLod to padding pixel count
    float atlasPadding = 8.0f;
    float maxSafeLod = std::floor(std::log2(atlasPadding));
    samplerInfo.maxLod = std::min(static_cast<float>(mipLevels), maxSafeLod);
    
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &fontSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create font texture sampler");
    }
}

void GridLabel::createDescriptorPool(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = maxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grid label descriptor pool");
    }
}

void GridLabel::createDescriptorSetLayout(VulkanDevice& vulkanDevice) {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grid label descriptor set layout");
    }
}

void GridLabel::createDescriptorSets(VulkanDevice& vulkanDevice, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate grid label descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBufferManager.getGridUniformBuffers()[i];
        bufferInfo.offset = uniformBufferManager.getGridUniformBufferOffsets()[i];
        bufferInfo.range = sizeof(GridUniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = fontAtlasView;
        imageInfo.sampler = fontSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()),
                              descriptorWrites.data(), 0, nullptr);
    }
}

void GridLabel::createPipeline(VulkanDevice& vulkanDevice, VkRenderPass renderPass) {
    auto vertShaderCode = readFile("shaders/grid_label_vert.spv");
    auto fragShaderCode = readFile("shaders/grid_label_frag.spv");

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

    // Vertex input
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(QuadVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> vertexAttributes{};
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[0].offset = offsetof(QuadVertex, position);

    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[1].offset = offsetof(QuadVertex, texCoord);

    // Instance input
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(LabelInstance);
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 5> instanceAttributes{};
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(LabelInstance, position);

    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    instanceAttributes[1].offset = offsetof(LabelInstance, charUV);

    instanceAttributes[2].binding = 1;
    instanceAttributes[2].location = 4;
    instanceAttributes[2].format = VK_FORMAT_R32_SFLOAT;
    instanceAttributes[2].offset = offsetof(LabelInstance, scale);

    instanceAttributes[3].binding = 1;
    instanceAttributes[3].location = 5; 
    instanceAttributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[3].offset = offsetof(LabelInstance, rightVec);

    instanceAttributes[4].binding = 1;
    instanceAttributes[4].location = 6;
    instanceAttributes[4].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[4].offset = offsetof(LabelInstance, upVec);

    std::array<VkVertexInputBindingDescription, 2> bindings = {vertexBinding, instanceBinding};
    // 2 (vertex) + 5 (instance)
    std::array<VkVertexInputAttributeDescription, 7> attributes;
    std::copy(vertexAttributes.begin(), vertexAttributes.end(), attributes.begin());
    std::copy(instanceAttributes.begin(), instanceAttributes.end(), attributes.begin() + 2);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
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
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target disabled for labels.
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target.
    colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

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
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grid label pipeline layout");
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
    pipelineInfo.subpass = 2;  // Same subpass as grid

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grid label graphics pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
}

void GridLabel::addEdgeLabels(const glm::vec3& basePos, int varyingAxis, float start, float end, float interval, float scale, const glm::vec3& textRight, const glm::vec3& textUp, bool includeOrigin, bool isBillboard) {
    
    // Label at Origin
    if (includeOrigin && start <= 0.001f && end >= -0.001f) {
        addTextInstances("0", basePos, scale, 0.6f, textRight, textUp, isBillboard);
    }
    
    // Positive Axis Labels 
    for (float t = interval; t <= end + 0.001f; t += interval) {
        glm::vec3 position = basePos;
        position[varyingAxis] = t;
        addTextInstances(floatToString(t, 1), position, scale, 0.6f, textRight, textUp, isBillboard);
    }

    // Negative Axis Labels 
    for (float t = -interval; t >= start - 0.001f; t -= interval) {
        glm::vec3 position = basePos;
        position[varyingAxis] = t;
        addTextInstances(floatToString(t, 1), position, scale, 0.6f, textRight, textUp, isBillboard);
    }
}

void GridLabel::generateLabelInstances(const glm::vec3& gridSize) {
    labelInstances.clear();
    float interval = 0.5f;
    float halfW = gridSize.x * 0.5f;
    float halfD = gridSize.y * 0.5f;
    float height = gridSize.z;
    float offset = 0.05f;
    float scale = 0.08f;

    // Y-Axis Labels (Vertical)
    const glm::vec3 yRight(-1.0f, 0.0f, 0.0f);
    const glm::vec3 yUp(0.0f, 1.0f, 0.0f);

    // X-Axis Labels (Floor)
    const glm::vec3 xRight(-1.0f, 0.0f, 0.0f); 
    const glm::vec3 xUp(0.0f, 0.0f, 1.0f);

    // Z-Axis Labels (Floor)
    const glm::vec3 zRight(0.0f, 0.0f, 1.0f);
    const glm::vec3 zUp(1.0f, 0.0f, 0.0f);

    // Y-Axis
    // Draw labels on all four corners with billboard flag enabled
    addEdgeLabels(glm::vec3(-halfW, 0, -halfD), 1, 0, height, interval, scale, yRight, yUp, true, true);
    addEdgeLabels(glm::vec3(halfW, 0, -halfD), 1, 0, height, interval, scale, yRight, yUp, true, true);
    addEdgeLabels(glm::vec3(halfW, 0, halfD), 1, 0, height, interval, scale, yRight, yUp, true, true);
    addEdgeLabels(glm::vec3(-halfW, 0, halfD), 1, 0, height, interval, scale, yRight, yUp, true, true);

    // Central X-Axis Labels (at Z=0)
    glm::vec3 xAxisBasePos = glm::vec3(-0.005f, 0.001f, -0.025f);
    addEdgeLabels(xAxisBasePos, 0, -halfW, halfW, interval, scale, xRight, xUp, false);

    // Central Z-Axis Labels (at X=0)
    glm::vec3 zAxisBasePos = glm::vec3(0.025f, 0.001f, 0.005f);
    addEdgeLabels(zAxisBasePos, 2, -halfD, halfD, interval, scale, zRight, zUp, false);
    
    instanceCount = static_cast<uint32_t>(labelInstances.size());
}

void GridLabel::addTextInstances(const std::string& text, const glm::vec3& position, float scale, float charSpacing, const glm::vec3& textRight, const glm::vec3& textUp, bool isBillboard) {
    if (text.empty()) 
        return;

    const float fontReferenceSize = 64.0f;
    const float advanceFactor = 0.225f;
    float metricConversionFactor = scale / fontReferenceSize;

    // Calculate total width for centering
    float totalWidth = 0.0f;
    for (char c : text) {
        int index = static_cast<int>(static_cast<unsigned char>(c));
        if (index < 0 || index >= static_cast<int>(charMap.size())) 
            continue;
        const CharInfo& info = charMap[index];
        totalWidth += info.xadvance * advanceFactor * metricConversionFactor;
    }
    
    float cursorX = -totalWidth * 0.5f;

    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        int index = static_cast<int>(static_cast<unsigned char>(c));
        if (index < 0 || index >= static_cast<int>(charMap.size())) 
            continue;

        const CharInfo& info = charMap[index];
        float quadWidthWorld = info.width * metricConversionFactor;
        float xoffsetWorld = info.xoffset * metricConversionFactor;
        float yoffsetWorld = info.yoffset * metricConversionFactor;
        
        float charCenterOffset = cursorX + xoffsetWorld + (quadWidthWorld * 0.5f);
        
        // Apply yoffset to shift the character down
        float charVerticalOffset = -yoffsetWorld;

        LabelInstance instance;
        instance.charUV = getCharUV(c);
        instance.scale = scale;
        instance.upVec = textUp;

        if (isBillboard) {
            instance.position = position;
            // For billboard, apply the offset to the rightVec 
            instance.rightVec = glm::vec3(charCenterOffset, charVerticalOffset, 0.0f);
        } else {
            instance.position = position + charCenterOffset * textRight + charVerticalOffset * textUp;
            instance.rightVec = textRight;
        }
        
        labelInstances.push_back(instance);
        cursorX += info.xadvance * advanceFactor * metricConversionFactor;
    }
}

void GridLabel::initializeCharMap() {
    // Initialize character map for 128 ASCII characters
    charMap.resize(128);
    
    // Set all characters map to nothing 
    for (int i = 0; i < 128; i++) {
        charMap[i] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }
    
    // Parse json
    std::ifstream jsonFile("textures/Roboto-Medium.json");
    if (!jsonFile.is_open()) {
        throw std::runtime_error("Failed to open json file");
    }
    
    std::string jsonContent((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    jsonFile.close();
    
    // Format: "chars":[{"id":48,"char":"0","width":64,"height":64,"x":0,"y":0},...
    const float atlasWidth = 856.0f;
    const float atlasHeight = 64.0f;
    
    // Parse each character entry
    size_t pos = jsonContent.find("\"chars\"");
    if (pos == std::string::npos) {
        throw std::runtime_error("Invalid JSON format: missing 'chars' array");
    }
    
    pos = jsonContent.find('[', pos);
    size_t endPos = jsonContent.find("]", pos);
    std::string charsStr = jsonContent.substr(pos + 1, endPos - pos - 1);
    
    // Split by character objects
    size_t charStart = 0;
    while ((charStart = charsStr.find('{', charStart)) != std::string::npos) {
        size_t charEnd = charsStr.find('}', charStart);
        std::string charObj = charsStr.substr(charStart, charEnd - charStart + 1);
        
        // Extract id, x, y, width, height
        auto extractValue = [&charObj](const std::string& key) -> float {
            size_t keyPos = charObj.find("\"" + key + "\":");
            if (keyPos == std::string::npos) return 0.0f;
            keyPos = charObj.find(':', keyPos) + 1;
            size_t valEnd = charObj.find_first_of(",}", keyPos);
            std::string valStr = charObj.substr(keyPos, valEnd - keyPos);
            // Remove whitespace
            valStr.erase(0, valStr.find_first_not_of(" \t\n\r"));
            valStr.erase(valStr.find_last_not_of(" \t\n\r") + 1);
            return std::stof(valStr);
        };
        
        int id = static_cast<int>(extractValue("id"));
        float x = extractValue("x");
        float y = extractValue("y");
        float width = extractValue("width");
        float height = extractValue("height");
        float xadvance = extractValue("xadvance");
        float xoffset = extractValue("xoffset");
        float yoffset = extractValue("yoffset");
        
        if (id >= 0 && id < 128) {
            charMap[id] = {
                x / atlasWidth,      // Normalize UV coordinates
                y / atlasHeight,     // Normalize UV coordinates
                width,               // Store raw pixel width
                height,              // Store raw pixel height
                xadvance,            // Store raw pixel xadvance
                xoffset,             // Store raw pixel xoffset
                yoffset              // Store raw pixel yoffset
            };
        }
        
        charStart = charEnd + 1;
    }
}

glm::vec4 GridLabel::getCharUV(char c) {
    int index = static_cast<int>(static_cast<unsigned char>(c));
    if (index < 0 || index >= static_cast<int>(charMap.size())) {
        return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);  // Invalid character
    }
    const CharInfo& info = charMap[index];
    // Return normalized UV coordinates and dimensions
    const float atlasWidth = 856.0f;
    const float atlasHeight = 64.0f;
    return glm::vec4(info.u, info.v, info.width / atlasWidth, info.height / atlasHeight);
}

std::string GridLabel::floatToString(float value, int precision) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

void GridLabel::updateLabels(const glm::vec3& gridSize) {
    // Only regenerate if grid size changed
    if (gridSize == cachedGridSize) 
        return;
    
    cachedGridSize = gridSize;
    generateLabelInstances(gridSize);
    
    // Update all instance buffers
    for (size_t i = 0; i < instanceBuffersMapped.size(); i++) {
        memcpy(instanceBuffersMapped[i], labelInstances.data(), 
               sizeof(LabelInstance) * labelInstances.size());
    }
}

void GridLabel::render(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (instanceCount == 0) 
        return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                           0, 1, &descriptorSets[currentFrame], 0, nullptr);

    VkBuffer vertexBuffers[] = {quadVertexBuffer, instanceBuffers[currentFrame]};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);

    vkCmdDraw(commandBuffer, 4, instanceCount, 0, 0);
}

void GridLabel::cleanup(VulkanDevice& vulkanDevice) {
    vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), descriptorPool, nullptr);
    
    vkDestroySampler(vulkanDevice.getDevice(), fontSampler, nullptr);
    vkDestroyImageView(vulkanDevice.getDevice(), fontAtlasView, nullptr);
    vkDestroyImage(vulkanDevice.getDevice(), fontAtlasImage, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), fontAtlasMemory, nullptr);
    
    for (size_t i = 0; i < instanceBuffers.size(); i++) {
        vkUnmapMemory(vulkanDevice.getDevice(), instanceBufferMemories[i]);
        vkDestroyBuffer(vulkanDevice.getDevice(), instanceBuffers[i], nullptr);
        vkFreeMemory(vulkanDevice.getDevice(), instanceBufferMemories[i], nullptr);
    }
    
    vkDestroyBuffer(vulkanDevice.getDevice(), quadVertexBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), quadVertexBufferMemory, nullptr);
}

void GridLabel::generateMipmaps(VkImage image, VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    // Check if format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(vulkanDevice.getPhysicalDevice(), format, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting");
    }

    VkCommandBuffer commandBuffer = commandPool.beginCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    commandPool.endCommands(commandBuffer);
}
