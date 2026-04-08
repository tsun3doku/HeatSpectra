#include "GizmoRenderer.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "util/File_utils.h"
#include "framegraph/FramePass.hpp"
#include "scene/GizmoController.hpp"
#include "scene/ModelSelection.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#include <tiny_obj_loader.h>

GizmoRenderer::GizmoRenderer(VulkanDevice& device, VkRenderPass renderPass, uint32_t subpassIndex, CommandPool& cmdPool)
    : vulkanDevice(device),
      renderCommandPool(cmdPool) {
    createGeometry();
    createPipeline(renderPass, subpassIndex);
}

GizmoRenderer::~GizmoRenderer() {
}

void GizmoRenderer::createGeometry() {
    createConeGeometry();
    createRingGeometry();
}

void GizmoRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) {
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        std::cerr << "[GizmoRenderer] Failed to create buffer" << std::endl;
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        std::cerr << "[GizmoRenderer] Failed to allocate buffer memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), buffer, memory, 0);
}

void GizmoRenderer::createConeGeometry() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, "models/gizmo_arrow.obj")) {
        std::cerr << "[GizmoRenderer] Failed to load gizmo_arrow.obj: " << warn << err << std::endl;
        return;
    }

    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;

    const size_t vertexCount = attrib.vertices.size() / 3;
    vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i].position = {
            attrib.vertices[3 * i + 0],
            attrib.vertices[3 * i + 1],
            attrib.vertices[3 * i + 2]
        };
        vertices[i].color = { 1.0f, 1.0f, 1.0f };
    }

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            indices.push_back(index.vertex_index);
        }
    }

    coneIndexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vertexBytes = sizeof(GizmoVertex) * vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();

    VkBuffer vertexStaging = VK_NULL_HANDLE;
    VkBuffer indexStaging = VK_NULL_HANDLE;
    VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexStagingMemory = VK_NULL_HANDLE;

    createBuffer(vertexBytes,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 vertexStaging,
                 vertexStagingMemory);
    createBuffer(indexBytes,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 indexStaging,
                 indexStagingMemory);

    void* data = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), vertexStagingMemory, 0, vertexBytes, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(vertexBytes));
    vkUnmapMemory(vulkanDevice.getDevice(), vertexStagingMemory);

    vkMapMemory(vulkanDevice.getDevice(), indexStagingMemory, 0, indexBytes, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(indexBytes));
    vkUnmapMemory(vulkanDevice.getDevice(), indexStagingMemory);

    createBuffer(vertexBytes,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 coneVertexBuffer,
                 coneVertexBufferMemory);
    createBuffer(indexBytes,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 coneIndexBuffer,
                 coneIndexBufferMemory);

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = vertexBytes;
    vkCmdCopyBuffer(cmd, vertexStaging, coneVertexBuffer, 1, &copyRegion);
    copyRegion.size = indexBytes;
    vkCmdCopyBuffer(cmd, indexStaging, coneIndexBuffer, 1, &copyRegion);
    renderCommandPool.endCommands(cmd);

    vkDestroyBuffer(vulkanDevice.getDevice(), vertexStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), vertexStagingMemory, nullptr);
    vkDestroyBuffer(vulkanDevice.getDevice(), indexStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), indexStagingMemory, nullptr);
}

void GizmoRenderer::createRingGeometry() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, "models/gizmo_ring.obj")) {
        std::cerr << "[GizmoRenderer] Failed to load gizmo_ring.obj: " << warn << err << std::endl;
        return;
    }

    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            GizmoVertex vertex{};
            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vertex.color = glm::vec3(1.0f);

            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
    }

    ringIndexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vertexBytes = sizeof(vertices[0]) * vertices.size();
    const VkDeviceSize indexBytes = sizeof(indices[0]) * indices.size();

    VkBuffer vertexStaging = VK_NULL_HANDLE;
    VkBuffer indexStaging = VK_NULL_HANDLE;
    VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexStagingMemory = VK_NULL_HANDLE;

    createBuffer(vertexBytes,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 vertexStaging,
                 vertexStagingMemory);
    createBuffer(indexBytes,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 indexStaging,
                 indexStagingMemory);

    void* data = nullptr;
    vkMapMemory(vulkanDevice.getDevice(), vertexStagingMemory, 0, vertexBytes, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(vertexBytes));
    vkUnmapMemory(vulkanDevice.getDevice(), vertexStagingMemory);

    vkMapMemory(vulkanDevice.getDevice(), indexStagingMemory, 0, indexBytes, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(indexBytes));
    vkUnmapMemory(vulkanDevice.getDevice(), indexStagingMemory);

    createBuffer(vertexBytes,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 ringVertexBuffer,
                 ringVertexBufferMemory);
    createBuffer(indexBytes,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 ringIndexBuffer,
                 ringIndexBufferMemory);

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = vertexBytes;
    vkCmdCopyBuffer(cmd, vertexStaging, ringVertexBuffer, 1, &copyRegion);
    copyRegion.size = indexBytes;
    vkCmdCopyBuffer(cmd, indexStaging, ringIndexBuffer, 1, &copyRegion);
    renderCommandPool.endCommands(cmd);

    vkDestroyBuffer(vulkanDevice.getDevice(), vertexStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), vertexStagingMemory, nullptr);
    vkDestroyBuffer(vulkanDevice.getDevice(), indexStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), indexStagingMemory, nullptr);
}

void GizmoRenderer::createPipeline(VkRenderPass renderPass, uint32_t subpassIndex) {
    auto vertCode = readFile("shaders/gizmo_vert.spv");
    auto fragCode = readFile("shaders/gizmo_frag.spv");

    VkShaderModule vertModule = createShaderModule(vulkanDevice, vertCode);
    VkShaderModule fragModule = createShaderModule(vulkanDevice, fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    const auto bindingDesc = GizmoVertex::getBindingDescription();
    const auto attrDescs = GizmoVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = nullptr;
    viewportState.pScissors = nullptr;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_TRUE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_REPLACE;
    stencilOp.failOp = VK_STENCIL_OP_KEEP;
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
    stencilOp.compareMask = 0xFF;
    stencilOp.writeMask = 0xFF;
    stencilOp.reference = 3;

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    VkPipelineColorBlendAttachmentState colorAttachments[2] = {};
    colorAttachments[0].colorWriteMask = 0;
    colorAttachments[0].blendEnable = VK_FALSE;
    colorAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttachments[1].blendEnable = VK_TRUE;
    colorAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorAttachments;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(GizmoPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[GizmoRenderer] Failed to create pipeline layout" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
        return;
    }

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
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
    pipelineInfo.subpass = subpassIndex;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "[GizmoRenderer] Failed to create pipeline" << std::endl;
        vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
        vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
        return;
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void GizmoRenderer::render(
    VkCommandBuffer commandBuffer,
    const glm::vec3& position,
    VkExtent2D extent,
    float scale,
    const render::SceneView& sceneView,
    const GizmoController& gizmoController) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    RenderState state{};
    state.commandBuffer = commandBuffer;
    state.position = position;
    state.scale = scale;
    state.distance = glm::distance(sceneView.cameraPosition, position);
    state.view = sceneView.view;
    state.proj = sceneView.proj;
    state.cameraFov = sceneView.cameraFov;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const GizmoAxis activeAxis = gizmoController.getActiveAxis();
    const GizmoMode currentMode = gizmoController.getMode();
    const bool isDragging = activeAxis != GizmoAxis::None;

    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::X)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 3);
        renderAxis(state, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.9f, 0.0f, 0.05f), false);
    }

    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::Y)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 4);
        renderAxis(state, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.05f, 0.9f, 0.0f), false);
    }

    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::Z)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 5);
        renderAxis(state, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.05f, 0.9f), false);
    }

    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::X)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 6);
        renderRotationRing(state, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.9f, 0.0f, 0.05f), false, 1.0f);
    }

    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::Z)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 8);
        renderRotationRing(state, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.05f, 0.9f), false, 0.97f);
    }

    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::Y)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 7);
        renderRotationRing(state, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.05f, 0.9f, 0.0f), false, 0.94f);
    }
}

void GizmoRenderer::renderAxis(const RenderState& state, const glm::vec3& direction, const glm::vec3& color, bool hovered) {
    const float offsetDistance = getArrowDistance(state.scale, state.distance, state.cameraFov);
    const float arrowScale = getArrowSize(state.scale, state.distance, state.cameraFov);
    const glm::vec3 offsetPosition = state.position + direction * offsetDistance;

    glm::mat4 rotation = glm::mat4(1.0f);
    if (direction.x > 0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    else if (direction.x < -0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    else if (direction.y > 0.5f) {
        rotation = glm::mat4(1.0f);
    }
    else if (direction.y < -0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    else if (direction.z > 0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    else if (direction.z < -0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), offsetPosition) *
        rotation *
        glm::scale(glm::mat4(1.0f), glm::vec3(arrowScale));

    GizmoPushConstants pc{};
    pc.model = model;
    pc.view = state.view;
    pc.proj = state.proj;
    pc.color = color;
    pc.hovered = hovered ? 1.0f : 0.0f;

    vkCmdPushConstants(
        state.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(GizmoPushConstants),
        &pc);

    VkDeviceSize offsets[] = { 0 };
    VkBuffer coneBuffers[] = { coneVertexBuffer };
    vkCmdBindVertexBuffers(state.commandBuffer, 0, 1, coneBuffers, offsets);
    vkCmdBindIndexBuffer(state.commandBuffer, coneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(state.commandBuffer, coneIndexCount, 1, 0, 0, 0);
}

void GizmoRenderer::renderRotationRing(const RenderState& state, const glm::vec3& axis, const glm::vec3& color, bool hovered, float radiusMultiplier) {
    const float scaled = applyDistanceScaling(state.scale, state.distance, state.cameraFov);
    const float ringScale = scaled * 0.75f * radiusMultiplier;

    glm::mat4 rotation = glm::mat4(1.0f);
    if (axis.x > 0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    else if (axis.y > 0.5f) {
        rotation = glm::mat4(1.0f);
    }
    else if (axis.z > 0.5f) {
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(ringScale));
    const glm::mat4 translateMatrix = glm::translate(glm::mat4(1.0f), state.position);
    const glm::mat4 model = translateMatrix * rotation * scaleMatrix;

    GizmoPushConstants pc{};
    pc.model = model;
    pc.view = state.view;
    pc.proj = state.proj;
    pc.color = color;
    pc.hovered = hovered ? 1.0f : 0.0f;

    vkCmdPushConstants(
        state.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(GizmoPushConstants),
        &pc);

    VkDeviceSize offsets[] = { 0 };
    VkBuffer ringBuffers[] = { ringVertexBuffer };
    vkCmdBindVertexBuffers(state.commandBuffer, 0, 1, ringBuffers, offsets);
    vkCmdBindIndexBuffer(state.commandBuffer, ringIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(state.commandBuffer, ringIndexCount, 1, 0, 0, 0);
}

float GizmoRenderer::calculateGizmoScale(ModelRegistry& resourceManager, const ModelSelection& modelSelection) const {
    const auto& selectedModelIDs = modelSelection.getSelectedModelIDsRenderThread();

    float maxBBoxSize = 0.0f;

    for (uint32_t id : selectedModelIDs) {
        glm::vec3 bboxMin(0.0f);
        glm::vec3 bboxMax(0.0f);
        if (resourceManager.tryGetBoundingBoxMinMax(id, bboxMin, bboxMax)) {
            const glm::vec3 bboxSize = bboxMax - bboxMin;
            maxBBoxSize = std::max(maxBBoxSize, std::max(bboxSize.x, std::max(bboxSize.y, bboxSize.z)));
        }
    }

    if (maxBBoxSize == 0.0f) {
        for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
            glm::vec3 bboxMin(0.0f);
            glm::vec3 bboxMax(0.0f);
            if (!resourceManager.tryGetBoundingBoxMinMax(modelId, bboxMin, bboxMax)) {
                continue;
            }

            const glm::vec3 bboxSize = bboxMax - bboxMin;
            maxBBoxSize = std::max(maxBBoxSize, std::max(bboxSize.x, std::max(bboxSize.y, bboxSize.z)));
        }
    }

    if (maxBBoxSize == 0.0f) {
        maxBBoxSize = 1.0f;
    }

    const float baseScaleMultiplier = 0.6f;
    float gizmoScale = maxBBoxSize * baseScaleMultiplier;

    const float minScale = 0.4f;
    const float maxScale = 1.0f;
    gizmoScale = glm::clamp(gizmoScale, minScale, maxScale);

    return gizmoScale;
}

float GizmoRenderer::applyDistanceScaling(float baseScale, float distance, float cameraFov) const {
    const float referenceDistance = 1.0f;
    const float maxScreenSpaceScale = 0.20f;
    const float cameraBaseFov = 45.0f;

    const float currentFov = cameraFov;
    const float tanBase = glm::tan(glm::radians(cameraBaseFov) / 2.0f);
    const float tanCurrent = glm::tan(glm::radians(currentFov) / 2.0f);
    const float fovFactor = tanCurrent / tanBase;

    const float maxBaseScale = maxScreenSpaceScale * referenceDistance;
    const float effectiveBaseScale = std::min(baseScale, maxBaseScale);

    float scaleFactor = (distance / referenceDistance) * fovFactor;
    scaleFactor = glm::clamp(scaleFactor, 0.01f, 50.0f);

    return effectiveBaseScale * scaleFactor;
}

float GizmoRenderer::getArrowSize(float baseScale, float distance, float cameraFov) const {
    const float arrowSizeMultiplier = 0.1f;
    const float scaled = applyDistanceScaling(baseScale, distance, cameraFov);
    return scaled * arrowSizeMultiplier;
}

float GizmoRenderer::getArrowDistance(float baseScale, float distance, float cameraFov) const {
    const float distanceMultiplier = 0.5f;
    const float scaled = applyDistanceScaling(baseScale, distance, cameraFov);
    return scaled * distanceMultiplier;
}

void GizmoRenderer::cleanup() {
    if (coneVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), coneVertexBuffer, nullptr);
        coneVertexBuffer = VK_NULL_HANDLE;
    }
    if (coneVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), coneVertexBufferMemory, nullptr);
        coneVertexBufferMemory = VK_NULL_HANDLE;
    }
    if (coneIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), coneIndexBuffer, nullptr);
        coneIndexBuffer = VK_NULL_HANDLE;
    }
    if (coneIndexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), coneIndexBufferMemory, nullptr);
        coneIndexBufferMemory = VK_NULL_HANDLE;
    }
    if (ringVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), ringVertexBuffer, nullptr);
        ringVertexBuffer = VK_NULL_HANDLE;
    }
    if (ringVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), ringVertexBufferMemory, nullptr);
        ringVertexBufferMemory = VK_NULL_HANDLE;
    }
    if (ringIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), ringIndexBuffer, nullptr);
        ringIndexBuffer = VK_NULL_HANDLE;
    }
    if (ringIndexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), ringIndexBufferMemory, nullptr);
        ringIndexBufferMemory = VK_NULL_HANDLE;
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

