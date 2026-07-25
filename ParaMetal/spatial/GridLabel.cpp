#include "GridLabel.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "util/file_utils.h"
#include "util/Structs.hpp"
#include <array>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include "libs/stb/stb_image.h"

GridLabel::GridLabel(VulkanDevice& vulkanDevice, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager,
                     uint32_t maxFramesInFlight, VkRenderPass renderPass, uint32_t subpass, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), allocator(allocator), uniformBufferManager(uniformBufferManager), commandPool(commandPool) {
    
    if (!glyphText.load()) {
        std::cerr << "[GridLabel] Failed to load glyph text" << std::endl;
        return;
    }
    createQuadVertexBuffer(vulkanDevice);
    createInstanceBuffer(vulkanDevice, maxFramesInFlight);
    createFontAtlas(vulkanDevice);
    createDescriptorPool(vulkanDevice, maxFramesInFlight);
    createDescriptorSetLayout(vulkanDevice);
    createDescriptorSets(vulkanDevice, uniformBufferManager, maxFramesInFlight);
    createPipeline(vulkanDevice, renderPass, subpass);
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

    const VkDeviceSize bufferSize = sizeof(QuadVertex) * vertices.size();

    auto [buffer, offset] = allocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (buffer == VK_NULL_HANDLE) {
        std::cerr << "[GridLabel] Failed to allocate quad vertex buffer" << std::endl;
        return;
    }

    quadVertexBuffer = buffer;
    quadVertexBufferOffset = offset;

    void* data = allocator.getMappedPointer(buffer, offset);
    if (data) {
        std::memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    }
}

void GridLabel::createInstanceBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    const VkDeviceSize bufferSize = sizeof(LabelInstance) * 1000;

    instanceBuffers.resize(maxFramesInFlight, VK_NULL_HANDLE);
    instanceBufferOffsets.resize(maxFramesInFlight, 0);
    instanceBuffersMapped.resize(maxFramesInFlight, nullptr);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, offset] = allocator.allocate(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (buffer == VK_NULL_HANDLE) {
            std::cerr << "[GridLabel] Failed to allocate instance buffer [" << i << "]" << std::endl;
            return;
        }

        instanceBuffers[i] = buffer;
        instanceBufferOffsets[i] = offset;
        instanceBuffersMapped[i] = allocator.getMappedPointer(buffer, offset);
    }
}

void GridLabel::createFontAtlas(VulkanDevice& vulkanDevice) {
    int texWidth, texHeight;
    const std::string& atlasPath = glyphText.getAtlasTexturePath();
    stbi_uc* pixels = stbi_load(atlasPath.c_str(), &texWidth, &texHeight, nullptr, STBI_rgb_alpha);
    
    if (!pixels) {
        std::cerr << "[GridLabel] Failed to load SDF font atlas: " << atlasPath << std::endl;
        return;
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
        std::cerr << "[GridLabel] Failed to create staging buffer" << std::endl;
        stbi_image_free(pixels);
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        std::cerr << "[GridLabel] Failed to allocate staging buffer memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return;
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
    
    if (generateImageMipmaps(
            vulkanDevice,
            commandPool,
            fontAtlasImage,
            VK_FORMAT_R8G8B8A8_UNORM,
            texWidth,
            texHeight,
            mipLevels) != VK_SUCCESS) {
        std::cerr << "[GridLabel] Failed to generate font atlas mipmaps" << std::endl;
    }
    
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
        std::cerr << "[GridLabel] Failed to create font texture sampler" << std::endl;
        return;
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
        std::cerr << "[GridLabel] Failed to create descriptor pool" << std::endl;
        return;
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
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[GridLabel] Failed to create descriptor set layout" << std::endl;
        return;
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
        std::cerr << "[GridLabel] Failed to allocate descriptor sets" << std::endl;
        return;
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

void GridLabel::createPipeline(VulkanDevice& vulkanDevice, VkRenderPass renderPass, uint32_t subpass) {
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

    VkPipelineColorBlendAttachmentState colorBlendAttachments[1] = {};
    // Line overlay target.
    colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
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
        std::cerr << "[GridLabel] Failed to create pipeline layout" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        return;
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

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[GridLabel] Failed to create graphics pipeline" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
        return;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
}

void GridLabel::addEdgeLabels(const glm::vec3& basePos, int varyingAxis, float start, float end, float interval, float scale, const glm::vec3& textRight, const glm::vec3& textUp, bool includeOrigin) {
    
    // Label at Origin
    if (includeOrigin && start <= 0.001f && end >= -0.001f) {
        addTextInstances("0", basePos, scale, 0.6f, textRight, textUp);
    }
    
    // Positive Axis Labels 
    for (float t = interval; t <= end + 0.001f; t += interval) {
        glm::vec3 position = basePos;
        position[varyingAxis] = t;
        addTextInstances(floatToString(t, 1), position, scale, 0.6f, textRight, textUp);
    }

    // Negative Axis Labels
    for (float t = -interval; t >= start - 0.001f; t -= interval) {
        glm::vec3 position = basePos;
        position[varyingAxis] = t;
        addTextInstances(floatToString(t, 1), position, scale, 0.6f, textRight, textUp);
    }
}

void GridLabel::generateLabelInstances(const glm::vec3& gridSize) {
    labelInstances.clear();
    float interval = 0.5f;
    float halfW = gridSize.x * 0.5f;
    float halfD = gridSize.y * 0.5f;
    float scale = 0.08f;

    // X-Axis Labels (Floor)
    const glm::vec3 xRight(-1.0f, 0.0f, 0.0f);
    const glm::vec3 xUp(0.0f, 0.0f, 1.0f);

    // Z-Axis Labels (Floor)
    const glm::vec3 zRight(0.0f, 0.0f, 1.0f);
    const glm::vec3 zUp(1.0f, 0.0f, 0.0f);

    // Central X-Axis Labels (at Z=0)
    glm::vec3 xAxisBasePos = glm::vec3(-0.005f, 0.001f, -0.025f);
    addEdgeLabels(xAxisBasePos, 0, -halfW, halfW, interval, scale, xRight, xUp, false);

    // Central Z-Axis Labels (at X=0)
    glm::vec3 zAxisBasePos = glm::vec3(0.025f, 0.001f, 0.005f);
    addEdgeLabels(zAxisBasePos, 2, -halfD, halfD, interval, scale, zRight, zUp, false);
    
    instanceCount = static_cast<uint32_t>(labelInstances.size());
}

void GridLabel::addTextInstances(const std::string& text, const glm::vec3& position, float scale, float charSpacing, const glm::vec3& textRight, const glm::vec3& textUp) {
    if (text.empty()) 
        return;

    const float fontReferenceSize = 64.0f;
    const float advanceFactor = 0.225f;
    float metricConversionFactor = scale / fontReferenceSize;

    // Calculate total width for centering
    float totalWidth = 0.0f;
    for (char c : text) {
        const GlyphText::CharInfo& info = glyphText.getCharInfo(c);
        if (info.width <= 0.0f || info.height <= 0.0f) {
            continue;
        }
        totalWidth += info.xadvance * advanceFactor * metricConversionFactor;
    }
    
    float cursorX = -totalWidth * 0.5f;

    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        const GlyphText::CharInfo& info = glyphText.getCharInfo(c);
        if (info.width <= 0.0f || info.height <= 0.0f) {
            continue;
        }
        float quadWidthWorld = info.width * metricConversionFactor;
        float xoffsetWorld = info.xoffset * metricConversionFactor;
        float yoffsetWorld = info.yoffset * metricConversionFactor;
        
        float charCenterOffset = cursorX + xoffsetWorld + (quadWidthWorld * 0.5f);
        
        // Apply yoffset to shift the character down
        float charVerticalOffset = yoffsetWorld;

        LabelInstance instance;
        instance.charUV = glyphText.getCharUV(c);
        if (instance.charUV.z <= 0.0f || instance.charUV.w <= 0.0f) {
            continue;
        }
        instance.scale = scale;
        instance.upVec = textUp;

        instance.position = position + charCenterOffset * textRight + charVerticalOffset * textUp;
        instance.rightVec = textRight;
        
        labelInstances.push_back(instance);
        cursorX += info.xadvance * advanceFactor * metricConversionFactor;
    }
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
    VkDeviceSize offsets[] = {quadVertexBufferOffset, instanceBufferOffsets[currentFrame]};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);

    vkCmdDraw(commandBuffer, 4, instanceCount, 0, 0);
}

void GridLabel::cleanup(VulkanDevice& vulkanDevice) {
    VkDevice device = vulkanDevice.getDevice();

    if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    if (descriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr); descriptorSetLayout = VK_NULL_HANDLE; }
    if (descriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, descriptorPool, nullptr); descriptorPool = VK_NULL_HANDLE; }

    if (fontSampler != VK_NULL_HANDLE) { vkDestroySampler(device, fontSampler, nullptr); fontSampler = VK_NULL_HANDLE; }
    if (fontAtlasView != VK_NULL_HANDLE) { vkDestroyImageView(device, fontAtlasView, nullptr); fontAtlasView = VK_NULL_HANDLE; }
    if (fontAtlasImage != VK_NULL_HANDLE) { vkDestroyImage(device, fontAtlasImage, nullptr); fontAtlasImage = VK_NULL_HANDLE; }
    if (fontAtlasMemory != VK_NULL_HANDLE) { vkFreeMemory(device, fontAtlasMemory, nullptr); fontAtlasMemory = VK_NULL_HANDLE; }

    for (size_t i = 0; i < instanceBuffers.size(); i++) {
        if (instanceBuffers[i] != VK_NULL_HANDLE) {
            allocator.free(instanceBuffers[i], instanceBufferOffsets[i]);
        }
    }

    if (quadVertexBuffer != VK_NULL_HANDLE) {
        allocator.free(quadVertexBuffer, quadVertexBufferOffset);
        quadVertexBuffer = VK_NULL_HANDLE;
    }
    quadVertexBufferOffset = 0;

    instanceBuffers.clear();
    instanceBufferOffsets.clear();
    instanceBuffersMapped.clear();
    descriptorSets.clear();
}
