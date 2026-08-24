#include "NavigationGizmoRenderer.hpp"

#include "scene/NavigationGizmoController.hpp"
#include "util/file_utils.h"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static constexpr uint32_t labelAtlasMipLevels = 5;
static constexpr float labelAtlasAnisotropy = 4.0f;

VkVertexInputBindingDescription NavigationGizmoVertex::bindingDescription() {
    VkVertexInputBindingDescription description{};
    description.binding = 0;
    description.stride = sizeof(NavigationGizmoVertex);
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return description;
}

std::array<VkVertexInputAttributeDescription, 3> NavigationGizmoVertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> descriptions{};
    descriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NavigationGizmoVertex, position)};
    descriptions[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NavigationGizmoVertex, normal)};
    descriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(NavigationGizmoVertex, uv)};
    return descriptions;
}

NavigationGizmoRenderer::NavigationGizmoRenderer(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    CommandPool& pool,
    VkRenderPass renderPass,
    uint32_t subpassIndex)
    : vulkanDevice(device), memoryAllocator(allocator), commandPool(pool) {
    ready = createGeometry() &&
        createLabelTexture() &&
        createDescriptors() &&
        createPipeline(renderPass, subpassIndex);
    if (!ready) {
        std::cerr << "[NavigationGizmoRenderer] Initialization failed" << std::endl;
    }
}

NavigationGizmoRenderer::~NavigationGizmoRenderer() = default;

bool NavigationGizmoRenderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = vulkanDevice.findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocation, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(vulkanDevice.getDevice(), buffer, memory, 0);
    return true;
}

bool NavigationGizmoRenderer::createGeometry() {
    std::vector<NavigationGizmoVertex> vertices;
    std::vector<uint16_t> indices;
    faceNormals = {
        glm::vec3(0, 0, -1), glm::vec3(0, 0, 1),
        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
        glm::vec3(0, 1, 0), glm::vec3(0, -1, 0)};
    const std::array<glm::vec3, 6> horizontalAxes = {
        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
        glm::vec3(0, 0, 1), glm::vec3(0, 0, -1),
        glm::vec3(1, 0, 0), glm::vec3(1, 0, 0)};
    const std::array<glm::vec3, 6> verticalAxes = {
        glm::vec3(0, 1, 0), glm::vec3(0, 1, 0),
        glm::vec3(0, 1, 0), glm::vec3(0, 1, 0),
        glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)};
    const std::array<glm::vec2, 4> coordinates = {
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)};
    for (uint16_t face = 0; face < faceNormals.size(); ++face) {
        const uint16_t baseVertex = static_cast<uint16_t>(vertices.size());
        for (const glm::vec2& coordinate : coordinates) {
            const glm::vec3 position = faceNormals[face] +
                horizontalAxes[face] * ((coordinate.x - 0.5f) * 2.0f) +
                verticalAxes[face] * ((coordinate.y - 0.5f) * 2.0f);
            vertices.push_back({position, faceNormals[face], coordinate});
        }
        indices.insert(indices.end(), {
            static_cast<uint16_t>(baseVertex + 0), static_cast<uint16_t>(baseVertex + 1), static_cast<uint16_t>(baseVertex + 2),
            static_cast<uint16_t>(baseVertex + 2), static_cast<uint16_t>(baseVertex + 3), static_cast<uint16_t>(baseVertex + 0)});
    }
    const VkDeviceSize vertexSize = vertices.size() * sizeof(NavigationGizmoVertex);
    const VkDeviceSize indexSize = indices.size() * sizeof(uint16_t);
    if (!createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexMemory) ||
        !createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexMemory)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), vertexMemory, 0, vertexSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexSize));
    vkUnmapMemory(vulkanDevice.getDevice(), vertexMemory);
    vkMapMemory(vulkanDevice.getDevice(), indexMemory, 0, indexSize, 0, &mapped);
    std::memcpy(mapped, indices.data(), static_cast<size_t>(indexSize));
    vkUnmapMemory(vulkanDevice.getDevice(), indexMemory);
    return true;
}

bool NavigationGizmoRenderer::createLabelTexture() {
    const std::string texturePath = "textures/icons/Overlays/viewcube/Artboard 1.png";
    if (createTextureImage(
            vulkanDevice,
            memoryAllocator,
            commandPool,
            texturePath,
            labelAtlasImage,
            labelAtlasMemory,
            labelAtlasMipLevels) != VK_SUCCESS) {
        std::cerr << "[NavigationGizmoRenderer] Failed to load " << texturePath << std::endl;
        return false;
    }
    if (createTextureImageView(
            vulkanDevice, labelAtlasImage, labelAtlasView, labelAtlasMipLevels) != VK_SUCCESS) {
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = (std::min)(
        labelAtlasAnisotropy,
        properties.limits.maxSamplerAnisotropy);
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(labelAtlasMipLevels - 1);
    return vkCreateSampler(
        vulkanDevice.getDevice(), &samplerInfo, nullptr, &labelAtlasSampler) == VK_SUCCESS;
}

bool NavigationGizmoRenderer::createDescriptors() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(
            vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(
            vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(
            vulkanDevice.getDevice(), &allocation, &descriptorSet) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = labelAtlasSampler;
    imageInfo.imageView = labelAtlasView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &write, 0, nullptr);
    return true;
}

bool NavigationGizmoRenderer::createPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    const std::vector<char> vertexCode = readFile("shaders/navigation_gizmo_vert.spv");
    const std::vector<char> fragmentCode = readFile("shaders/navigation_gizmo_frag.spv");
    const VkShaderModule vertexShader = createShaderModule(vulkanDevice, vertexCode);
    const VkShaderModule fragmentShader = createShaderModule(vulkanDevice, fragmentCode);
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr};
    const VkVertexInputBindingDescription binding = NavigationGizmoVertex::bindingDescription();
    const auto attributes = NavigationGizmoVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &attachment;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = states;
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    bool success = vkCreatePipelineLayout(
        vulkanDevice.getDevice(), &layoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS;
    if (success) {
        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = subpassIndex;
        success = vkCreateGraphicsPipelines(
            vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;
    }
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertexShader, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragmentShader, nullptr);
    return success;
}

void NavigationGizmoRenderer::render(
    VkCommandBuffer commandBuffer,
    const NavigationGizmoRenderData& data) {
    if (!ready || commandBuffer == VK_NULL_HANDLE || data.sizePx <= 0.0f) {
        return;
    }
    VkViewport viewport{};
    viewport.x = data.originPx.x;
    viewport.y = data.originPx.y;
    viewport.width = data.sizePx;
    viewport.height = data.sizePx;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.offset = {static_cast<int32_t>(data.originPx.x), static_cast<int32_t>(data.originPx.y)};
    scissor.extent = {static_cast<uint32_t>(data.sizePx), static_cast<uint32_t>(data.sizePx)};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    const glm::quat inverseOrientation = glm::conjugate(data.cameraOrientation);
    PushConstants pushConstants{};
    pushConstants.rotation = glm::mat4_cast(inverseOrientation);
    pushConstants.hoveredRegion = data.hoveredRegion;
    pushConstants.pressedRegion = data.pressedRegion;
    vkCmdPushConstants(commandBuffer, pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pushConstants), &pushConstants);
    for (uint32_t face = 0; face < faceNormals.size(); ++face) {
        if ((inverseOrientation * faceNormals[face]).z > 0.001f) {
            vkCmdDrawIndexed(commandBuffer, 6, 1, face * 6, 0, 0);
        }
    }
    viewport = {0.0f, 0.0f, static_cast<float>(data.extent.width),
        static_cast<float>(data.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    scissor = {{0, 0}, data.extent};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void NavigationGizmoRenderer::cleanup() {
    ready = false;
    const VkDevice device = vulkanDevice.getDevice();
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (labelAtlasSampler != VK_NULL_HANDLE) vkDestroySampler(device, labelAtlasSampler, nullptr);
    if (labelAtlasView != VK_NULL_HANDLE) vkDestroyImageView(device, labelAtlasView, nullptr);
    if (labelAtlasImage != VK_NULL_HANDLE) vkDestroyImage(device, labelAtlasImage, nullptr);
    if (labelAtlasMemory != VK_NULL_HANDLE) memoryAllocator.freeImageMemory(labelAtlasMemory);
    if (vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, vertexBuffer, nullptr);
    if (vertexMemory != VK_NULL_HANDLE) vkFreeMemory(device, vertexMemory, nullptr);
    if (indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, indexBuffer, nullptr);
    if (indexMemory != VK_NULL_HANDLE) vkFreeMemory(device, indexMemory, nullptr);
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
    labelAtlasSampler = VK_NULL_HANDLE;
    labelAtlasView = VK_NULL_HANDLE;
    labelAtlasImage = VK_NULL_HANDLE;
    labelAtlasMemory = VK_NULL_HANDLE;
    vertexBuffer = VK_NULL_HANDLE;
    vertexMemory = VK_NULL_HANDLE;
    indexBuffer = VK_NULL_HANDLE;
    indexMemory = VK_NULL_HANDLE;
}
