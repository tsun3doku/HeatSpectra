#include "ScreenTextRenderer.hpp"

#include "libs/stb/stb_image.h"
#include "util/file_utils.h"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <array>
#include <cstring>
#include <iostream>

ScreenTextRenderer::ScreenTextRenderer(
    VulkanDevice& device,
    MemoryAllocator& memoryAllocator,
    uint32_t maxFramesInFlight,
    VkRenderPass renderPass,
    uint32_t subpassIndex,
    CommandPool& pool)
    : vulkanDevice(device), allocator(memoryAllocator), commandPool(pool) {
    ready = glyphText.load() &&
        createQuadVertexBuffer() &&
        createInstanceBuffers(maxFramesInFlight) &&
        createFontAtlas() &&
        createDescriptors(maxFramesInFlight) &&
        createPipeline(renderPass, subpassIndex);
    if (!ready) {
        std::cerr << "[ScreenTextRenderer] Initialization failed" << std::endl;
    }
}

ScreenTextRenderer::~ScreenTextRenderer() = default;

bool ScreenTextRenderer::createQuadVertexBuffer() {
    const std::array<QuadVertex, 4> vertices = {
        QuadVertex{{-0.5f, -0.5f}, {0.0f, 1.0f}},
        QuadVertex{{ 0.5f, -0.5f}, {1.0f, 1.0f}},
        QuadVertex{{ 0.5f,  0.5f}, {1.0f, 0.0f}},
        QuadVertex{{-0.5f,  0.5f}, {0.0f, 0.0f}}};
    const VkDeviceSize size = sizeof(QuadVertex) * vertices.size();
    auto allocation = allocator.allocate(
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    quadVertexBuffer = allocation.first;
    quadVertexBufferOffset = allocation.second;
    if (quadVertexBuffer == VK_NULL_HANDLE) {
        return false;
    }
    void* mapped = allocator.getMappedPointer(quadVertexBuffer, quadVertexBufferOffset);
    if (!mapped) {
        return false;
    }
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
    return true;
}

bool ScreenTextRenderer::createInstanceBuffers(uint32_t maxFramesInFlight) {
    const VkDeviceSize size = sizeof(GlyphText::GlyphInstance) * maxGlyphCapacity;
    instanceBuffers.resize(maxFramesInFlight, VK_NULL_HANDLE);
    instanceBufferOffsets.resize(maxFramesInFlight, 0);
    instanceBuffersMapped.resize(maxFramesInFlight, nullptr);
    frameGlyphCursors.resize(maxFramesInFlight, 0);
    for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
        auto allocation = allocator.allocate(
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        instanceBuffers[frame] = allocation.first;
        instanceBufferOffsets[frame] = allocation.second;
        if (instanceBuffers[frame] == VK_NULL_HANDLE) {
            return false;
        }
        instanceBuffersMapped[frame] = allocator.getMappedPointer(
            instanceBuffers[frame], instanceBufferOffsets[frame]);
        if (!instanceBuffersMapped[frame]) {
            return false;
        }
    }
    return true;
}

bool ScreenTextRenderer::createFontAtlas() {
    int width = 0;
    int height = 0;
    stbi_uc* pixels = stbi_load(glyphText.getAtlasTexturePath().c_str(), &width, &height, nullptr, STBI_rgb_alpha);
    if (!pixels) {
        return false;
    }
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &requirements);
    VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vulkanDevice.getDevice(), &memoryInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return false;
    }
    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingMemory, 0);
    void* mapped = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingMemory);
    stbi_image_free(pixels);
    createImage(
        vulkanDevice,
        allocator,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        fontAtlasImage,
        fontAtlasMemory,
        VK_SAMPLE_COUNT_1_BIT,
        1);
    transitionImageLayout(commandPool, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    commandPool.copyBufferToImage(stagingBuffer, fontAtlasImage,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    transitionImageLayout(commandPool, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    fontAtlasView = createImageView(
        vulkanDevice, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingMemory, nullptr);
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return fontAtlasView != VK_NULL_HANDLE &&
        vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &fontSampler) == VK_SUCCESS;
}

bool ScreenTextRenderer::createDescriptors(uint32_t maxFramesInFlight) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(
            vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;
    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptorPool;
    allocation.descriptorSetCount = maxFramesInFlight;
    allocation.pSetLayouts = layouts.data();
    descriptorSets.resize(maxFramesInFlight, VK_NULL_HANDLE);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocation, descriptorSets.data()) != VK_SUCCESS) {
        return false;
    }
    for (VkDescriptorSet descriptorSet : descriptorSets) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = fontAtlasView;
        imageInfo.sampler = fontSampler;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &write, 0, nullptr);
    }
    return true;
}

bool ScreenTextRenderer::createPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    const std::vector<char> vertCode = readFile("shaders/timing_overlay_vert.spv");
    const std::vector<char> fragCode = readFile("shaders/timing_overlay_frag.spv");
    const VkShaderModule vert = createShaderModule(vulkanDevice, vertCode);
    const VkShaderModule frag = createShaderModule(vulkanDevice, fragCode);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0] = {0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    bindings[1] = {1, sizeof(GlyphText::GlyphInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
    std::array<VkVertexInputAttributeDescription, 6> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, texCoord)};
    attributes[2] = {2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphText::GlyphInstance, centerPx)};
    attributes[3] = {3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(GlyphText::GlyphInstance, sizePx)};
    attributes[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GlyphText::GlyphInstance, charUV)};
    attributes[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GlyphText::GlyphInstance, color)};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
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
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptorSetLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    bool ok = vkCreatePipelineLayout(vulkanDevice.getDevice(), &layout, nullptr, &pipelineLayout) == VK_SUCCESS;
    if (ok) {
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        info.subpass = subpassIndex;
        ok = vkCreateGraphicsPipelines(
            vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) == VK_SUCCESS;
    }
    vkDestroyShaderModule(vulkanDevice.getDevice(), vert, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), frag, nullptr);
    return ok;
}

void ScreenTextRenderer::beginFrame(uint32_t currentFrame) {
    if (currentFrame < frameGlyphCursors.size()) {
        frameGlyphCursors[currentFrame] = 0;
    }
}

bool ScreenTextRenderer::draw(
    VkCommandBuffer commandBuffer,
    uint32_t currentFrame,
    VkExtent2D extent,
    const std::vector<GlyphText::GlyphInstance>& glyphs) {
    if (!ready || glyphs.empty() || currentFrame >= frameGlyphCursors.size()) {
        return false;
    }
    const uint32_t firstGlyph = frameGlyphCursors[currentFrame];
    if (firstGlyph + glyphs.size() > maxGlyphCapacity) {
        std::cerr << "[ScreenTextRenderer] Per-frame glyph capacity exceeded" << std::endl;
        return false;
    }
    auto* destination = static_cast<uint8_t*>(instanceBuffersMapped[currentFrame]) +
        sizeof(GlyphText::GlyphInstance) * firstGlyph;
    std::memcpy(destination, glyphs.data(), sizeof(GlyphText::GlyphInstance) * glyphs.size());
    frameGlyphCursors[currentFrame] += static_cast<uint32_t>(glyphs.size());
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
    const glm::vec2 viewportSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(viewportSize), &viewportSize);
    VkBuffer buffers[2] = {quadVertexBuffer, instanceBuffers[currentFrame]};
    const VkDeviceSize offsets[2] = {quadVertexBufferOffset, instanceBufferOffsets[currentFrame]};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
    vkCmdDraw(commandBuffer, 4, static_cast<uint32_t>(glyphs.size()), 0, firstGlyph);
    return true;
}

void ScreenTextRenderer::cleanup() {
    ready = false;
    const VkDevice device = vulkanDevice.getDevice();
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (fontSampler != VK_NULL_HANDLE) vkDestroySampler(device, fontSampler, nullptr);
    if (fontAtlasView != VK_NULL_HANDLE) vkDestroyImageView(device, fontAtlasView, nullptr);
    if (fontAtlasImage != VK_NULL_HANDLE) vkDestroyImage(device, fontAtlasImage, nullptr);
    if (fontAtlasMemory != VK_NULL_HANDLE) allocator.freeImageMemory(fontAtlasMemory);
    for (size_t frame = 0; frame < instanceBuffers.size(); ++frame) {
        if (instanceBuffers[frame] != VK_NULL_HANDLE) {
            allocator.free(instanceBuffers[frame], instanceBufferOffsets[frame]);
        }
    }
    if (quadVertexBuffer != VK_NULL_HANDLE) {
        allocator.free(quadVertexBuffer, quadVertexBufferOffset);
    }
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    fontSampler = VK_NULL_HANDLE;
    fontAtlasView = VK_NULL_HANDLE;
    fontAtlasImage = VK_NULL_HANDLE;
    fontAtlasMemory = VK_NULL_HANDLE;
    quadVertexBuffer = VK_NULL_HANDLE;
    instanceBuffers.clear();
    instanceBufferOffsets.clear();
    instanceBuffersMapped.clear();
    frameGlyphCursors.clear();
    descriptorSets.clear();
}
