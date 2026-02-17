#include "TimingOverlay.hpp"

#include "VulkanDevice.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "file_utils.h"
#include "libs/stb/stb_image.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

float readJsonNumber(const std::string& text, const std::string& key, float fallback) {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return fallback;
    }

    size_t pos = keyPos + key.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }

    size_t end = pos;
    while (end < text.size()) {
        const char c = text[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            ++end;
            continue;
        }
        break;
    }

    if (end <= pos) {
        return fallback;
    }

    return std::stof(text.substr(pos, end - pos));
}

} // namespace

TimingOverlay::TimingOverlay(
    VulkanDevice& vulkanDevice,
    uint32_t maxFramesInFlight,
    VkRenderPass renderPass,
    uint32_t subpassIndex,
    CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      commandPool(commandPool) {
    initializeCharMap();
    createQuadVertexBuffer();
    createInstanceBuffers(maxFramesInFlight);
    createFontAtlas();
    createDescriptorPool(maxFramesInFlight);
    createDescriptorSetLayout();
    createDescriptorSets(maxFramesInFlight);
    createPipeline(renderPass, subpassIndex);
}

TimingOverlay::~TimingOverlay() {
}

void TimingOverlay::createQuadVertexBuffer() {
    const std::array<QuadVertex, 4> vertices = {
        QuadVertex{{-0.5f, -0.5f}, {0.0f, 1.0f}},
        QuadVertex{{ 0.5f, -0.5f}, {1.0f, 1.0f}},
        QuadVertex{{ 0.5f,  0.5f}, {1.0f, 0.0f}},
        QuadVertex{{-0.5f,  0.5f}, {0.0f, 0.0f}},
    };

    const VkDeviceSize size = sizeof(QuadVertex) * vertices.size();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &quadVertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timing overlay quad buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), quadVertexBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &quadVertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate timing overlay quad memory");
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), quadVertexBuffer, quadVertexBufferMemory, 0);

    void* mapped = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), quadVertexBufferMemory, 0, size, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
    vkUnmapMemory(vulkanDevice.getDevice(), quadVertexBufferMemory);
}

void TimingOverlay::createInstanceBuffers(uint32_t maxFramesInFlight) {
    const VkDeviceSize size = sizeof(GlyphInstance) * maxGlyphCapacity;

    instanceBuffers.resize(maxFramesInFlight, VK_NULL_HANDLE);
    instanceBufferMemories.resize(maxFramesInFlight, VK_NULL_HANDLE);
    instanceBuffersMapped.resize(maxFramesInFlight, nullptr);

    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &instanceBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create timing overlay instance buffer");
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), instanceBuffers[i], &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &instanceBufferMemories[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate timing overlay instance memory");
        }

        vkBindBufferMemory(vulkanDevice.getDevice(), instanceBuffers[i], instanceBufferMemories[i], 0);
        vkMapMemory(vulkanDevice.getDevice(), instanceBufferMemories[i], 0, size, 0, &instanceBuffersMapped[i]);
    }
}

void TimingOverlay::createFontAtlas() {
    int width = 0;
    int height = 0;
    stbi_uc* pixels = stbi_load("textures/Roboto-Medium-timing.png", &width, &height, nullptr, STBI_rgb_alpha);
    if (!pixels) {
        pixels = stbi_load("textures/Roboto-Medium.png", &width, &height, nullptr, STBI_rgb_alpha);
    }
    if (!pixels) {
        throw std::runtime_error("Failed to load timing overlay atlas");
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice.getDevice(), &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        throw std::runtime_error("Failed to create timing overlay staging buffer");
    }

    VkMemoryRequirements stagingReq{};
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &stagingReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = stagingReq.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
        stagingReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stbi_image_free(pixels);
        throw std::runtime_error("Failed to allocate timing overlay staging memory");
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingMemory, 0);

    void* mapped = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vulkanDevice.getDevice(), stagingMemory);
    stbi_image_free(pixels);

    createImage(
        vulkanDevice,
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

    transitionImageLayout(commandPool, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    commandPool.copyBufferToImage(stagingBuffer, fontAtlasImage, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    transitionImageLayout(commandPool, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

    fontAtlasView = createImageView(vulkanDevice, fontAtlasImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), stagingMemory, nullptr);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &fontSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timing overlay sampler");
    }
}

void TimingOverlay::createDescriptorPool(uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timing overlay descriptor pool");
    }
}

void TimingOverlay::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timing overlay descriptor set layout");
    }
}

void TimingOverlay::createDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(maxFramesInFlight, VK_NULL_HANDLE);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate timing overlay descriptor sets");
    }

    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = fontAtlasView;
        imageInfo.sampler = fontSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &write, 0, nullptr);
    }
}

void TimingOverlay::createPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    const std::vector<char> vertCode = readFile("shaders/timing_overlay_vert.spv");
    const std::vector<char> fragCode = readFile("shaders/timing_overlay_frag.spv");

    VkShaderModule vertShader = createShaderModule(vulkanDevice, vertCode);
    VkShaderModule fragShader = createShaderModule(vulkanDevice, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShader;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(QuadVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(GlyphInstance);
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputBindingDescription, 2> bindings = { vertexBinding, instanceBinding };

    std::array<VkVertexInputAttributeDescription, 6> attrs{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(QuadVertex, position)) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(QuadVertex, texCoord)) };
    attrs[2] = { 2, 1, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GlyphInstance, centerPx)) };
    attrs[3] = { 3, 1, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GlyphInstance, sizePx)) };
    attrs[4] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(GlyphInstance, charUV)) };
    attrs[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(GlyphInstance, color)) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState blendAttachments[2] = {};
    blendAttachments[0].colorWriteMask = 0;
    blendAttachments[0].blendEnable = VK_FALSE;
    blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachments[1].blendEnable = VK_TRUE;
    blendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 2;
    colorBlend.pAttachments = blendAttachments;

    std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::vec2);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShader, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShader, nullptr);
        throw std::runtime_error("Failed to create timing overlay pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassIndex;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertShader, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragShader, nullptr);
        throw std::runtime_error("Failed to create timing overlay pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShader, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShader, nullptr);
}

void TimingOverlay::initializeCharMap() {
    charMap.assign(128, {});

    std::ifstream jsonFile("textures/Roboto-Medium-timing.json");
    if (!jsonFile.is_open()) {
        jsonFile.open("textures/Roboto-Medium.json");
    }
    if (!jsonFile.is_open()) {
        throw std::runtime_error("Failed to open font metadata json");
    }
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());

    atlasWidth = readJsonNumber(json, "\"scaleW\":", 856.0f);
    atlasHeight = readJsonNumber(json, "\"scaleH\":", 64.0f);
    if (atlasWidth <= 0.0f) atlasWidth = 856.0f;
    if (atlasHeight <= 0.0f) atlasHeight = 64.0f;

    size_t cursor = json.find("\"chars\"");
    cursor = (cursor == std::string::npos) ? cursor : json.find('[', cursor);
    if (cursor == std::string::npos) {
        throw std::runtime_error("Missing chars array in font metadata");
    }

    while (true) {
        const size_t start = json.find('{', cursor);
        if (start == std::string::npos) break;
        const size_t end = json.find('}', start);
        if (end == std::string::npos) break;
        const std::string item = json.substr(start, end - start + 1);

        const int id = static_cast<int>(readJsonNumber(item, "\"id\":", -1.0f));
        if (id >= 0 && id < static_cast<int>(charMap.size())) {
            CharInfo info{};
            const float x = readJsonNumber(item, "\"x\":", 0.0f);
            const float y = readJsonNumber(item, "\"y\":", 0.0f);
            info.width = readJsonNumber(item, "\"width\":", 0.0f);
            info.height = readJsonNumber(item, "\"height\":", 0.0f);
            info.xadvance = readJsonNumber(item, "\"xadvance\":", info.width);
            info.xoffset = readJsonNumber(item, "\"xoffset\":", 0.0f);
            info.yoffset = readJsonNumber(item, "\"yoffset\":", 0.0f);
            info.u = x / atlasWidth;
            info.v = y / atlasHeight;
            charMap[static_cast<size_t>(id)] = info;
        }

        cursor = end + 1;
    }
}

glm::vec4 TimingOverlay::getCharUV(char c) const {
    const uint32_t index = static_cast<uint32_t>(static_cast<unsigned char>(c));
    if (index >= charMap.size()) {
        return glm::vec4(0.0f);
    }

    const CharInfo& info = charMap[index];
    if (info.width <= 0.0f || info.height <= 0.0f) {
        return glm::vec4(0.0f);
    }

    return glm::vec4(info.u, info.v, info.width / atlasWidth, info.height / atlasHeight);
}

void TimingOverlay::buildGlyphInstances() {
    glyphInstances.clear();
    const float scale = 18.0f / 64.0f;
    const float advanceFactor = 0.4f;
    const float marginX = 8.0f;
    const float marginY = 8.0f;
    const float lineSpacing = 18.0f;
    const glm::vec4 labelColor(0.82f, 0.84f, 0.86f, 1.0f);
    const glm::vec4 valueColor(0.2f, 0.75f, 0.25f, 1.0f);

    float lineTop = marginY;
    for (const std::string& line : activeLines) {
        const size_t separatorPos = line.find(':');
        float cursorX = marginX;
        for (size_t charIndex = 0; charIndex < line.size(); ++charIndex) {
            const char c = line[charIndex];
            const uint32_t index = static_cast<uint32_t>(static_cast<unsigned char>(c));

            if (index >= charMap.size()) 
                continue;

            const CharInfo& info = charMap[index];

            if (info.width > 0.0f && info.height > 0.0f) {
                GlyphInstance glyph{};
                glyph.centerPx = glm::vec2(
                    cursorX + (info.xoffset * scale) + (0.5f * info.width * scale),
                    lineTop + (info.yoffset * scale) + (0.5f * info.height * scale));

                glyph.sizePx = glm::vec2(info.width * scale, info.height * scale);
                glyph.charUV = getCharUV(c);
                const bool isValueText = (separatorPos != std::string::npos) && (charIndex > separatorPos);
                glyph.color = isValueText ? valueColor : labelColor;

                if (glyph.charUV.z > 0.0f && glyph.charUV.w > 0.0f) {
                    glyphInstances.push_back(glyph);

                    if (glyphInstances.size() >= maxGlyphCapacity) 
                        break;
                }
            }

            cursorX += info.xadvance * scale * advanceFactor;
        }

        if (glyphInstances.size() >= maxGlyphCapacity) 
            break;

        lineTop += lineSpacing;
    }

    glyphCount = static_cast<uint32_t>(glyphInstances.size());
}

void TimingOverlay::setLines(const std::vector<std::string>& lines) {
    if (lines == activeLines)
        return;
    activeLines = lines;
    buildGlyphInstances();

    for (void* mapped : instanceBuffersMapped) {
        if (!mapped) 
            continue;

        if (!glyphInstances.empty()) {
            std::memcpy(mapped, glyphInstances.data(), sizeof(GlyphInstance) * glyphInstances.size());
        }
    }
}

void TimingOverlay::render(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkExtent2D extent) {
    if (pipeline == VK_NULL_HANDLE || glyphCount == 0) 
        return;

    if (currentFrame >= descriptorSets.size() || currentFrame >= instanceBuffers.size()) 
        return;

    const glm::vec2 viewportSize(static_cast<float>(extent.width), static_cast<float>(extent.height));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2), &viewportSize);

    VkBuffer buffers[2] = { quadVertexBuffer, instanceBuffers[currentFrame] };
    VkDeviceSize offsets[2] = { 0, 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
    vkCmdDraw(commandBuffer, 4, glyphCount, 0, 0);
}

void TimingOverlay::cleanup() {
    VkDevice device = vulkanDevice.getDevice();

    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (fontSampler != VK_NULL_HANDLE) vkDestroySampler(device, fontSampler, nullptr);
    if (fontAtlasView != VK_NULL_HANDLE) vkDestroyImageView(device, fontAtlasView, nullptr);
    if (fontAtlasImage != VK_NULL_HANDLE) vkDestroyImage(device, fontAtlasImage, nullptr);
    if (fontAtlasMemory != VK_NULL_HANDLE) vkFreeMemory(device, fontAtlasMemory, nullptr);

    for (size_t i = 0; i < instanceBuffers.size(); ++i) {
        if (instanceBufferMemories[i] != VK_NULL_HANDLE && instanceBuffersMapped[i] != nullptr) {
            vkUnmapMemory(device, instanceBufferMemories[i]);
        }
        if (instanceBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(device, instanceBuffers[i], nullptr);
        if (instanceBufferMemories[i] != VK_NULL_HANDLE) vkFreeMemory(device, instanceBufferMemories[i], nullptr);
    }

    if (quadVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, quadVertexBuffer, nullptr);
    if (quadVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, quadVertexBufferMemory, nullptr);

    quadVertexBuffer = VK_NULL_HANDLE;
    quadVertexBufferMemory = VK_NULL_HANDLE;
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    fontSampler = VK_NULL_HANDLE;
    fontAtlasView = VK_NULL_HANDLE;
    fontAtlasImage = VK_NULL_HANDLE;
    fontAtlasMemory = VK_NULL_HANDLE;
}
