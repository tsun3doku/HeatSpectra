#include "Gizmo.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp"
#include "Camera.hpp"
#include "File_utils.h"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "ResourceManager.hpp"
#include "ModelSelection.hpp"
#include "Model.hpp"
#include <stdexcept>
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <tiny_obj_loader.h>

static void createBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device.getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device.getDevice(), buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties);
    
    if (vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }
    
    vkBindBufferMemory(device.getDevice(), buffer, memory, 0);
}

Gizmo::Gizmo(VulkanDevice& device, MemoryAllocator& allocator, Camera& camera,
             VkRenderPass renderPass, VkExtent2D extent, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), camera(camera), renderCommandPool(cmdPool),
      pipeline(VK_NULL_HANDLE), pipelineLayout(VK_NULL_HANDLE),
      coneVertexBuffer(VK_NULL_HANDLE), coneVertexBufferMemory(VK_NULL_HANDLE),
      coneIndexBuffer(VK_NULL_HANDLE), coneIndexBufferMemory(VK_NULL_HANDLE),
      ringVertexBuffer(VK_NULL_HANDLE), ringVertexBufferMemory(VK_NULL_HANDLE),
      ringIndexBuffer(VK_NULL_HANDLE), ringIndexBufferMemory(VK_NULL_HANDLE),
      currentMode(GizmoMode::Translate), activeAxis(GizmoAxis::None),
      coneIndexCount(0), ringIndexCount(0) {
    createGeometry();
    createPipeline(renderPass, extent);
}

Gizmo::~Gizmo() {}

void Gizmo::createGeometry() {
    createConeGeometry();
    createRingGeometry();
}

void Gizmo::createConeGeometry() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn, err;
    
    if (!tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, "models/gizmo_arrow.obj")) {
        throw std::runtime_error("Failed to load gizmo_arrow.obj: " + warn + err);
    }
    
    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;
    
    // Create vertices from OBJ
    size_t vertexCount = attrib.vertices.size() / 3;
    vertices.resize(vertexCount);
    
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i].position = {
            attrib.vertices[3 * i + 0],
            attrib.vertices[3 * i + 1],
            attrib.vertices[3 * i + 2]
        };
        vertices[i].color = {1.0f, 1.0f, 1.0f};
    }
    
    // Extract indices from shapes
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            indices.push_back(index.vertex_index);
        }
    }
    
    coneIndexCount = static_cast<uint32_t>(indices.size());
    
    VkDeviceSize vSize = sizeof(GizmoVertex) * vertices.size();
    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
    
    VkBuffer vStaging, iStaging;
    VkDeviceMemory vStagingMem, iStagingMem;
    
    createBuffer(vulkanDevice, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vStaging, vStagingMem);
    createBuffer(vulkanDevice, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, iStaging, iStagingMem);
    
    void* data;
    vkMapMemory(vulkanDevice.getDevice(), vStagingMem, 0, vSize, 0, &data);
    memcpy(data, vertices.data(), vSize);
    vkUnmapMemory(vulkanDevice.getDevice(), vStagingMem);
    
    vkMapMemory(vulkanDevice.getDevice(), iStagingMem, 0, iSize, 0, &data);
    memcpy(data, indices.data(), iSize);
    vkUnmapMemory(vulkanDevice.getDevice(), iStagingMem);
    
    createBuffer(vulkanDevice, vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, coneVertexBuffer, coneVertexBufferMemory);
    createBuffer(vulkanDevice, iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, coneIndexBuffer, coneIndexBufferMemory);
    
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = vSize;
    vkCmdCopyBuffer(cmd, vStaging, coneVertexBuffer, 1, &copyRegion);
    copyRegion.size = iSize;
    vkCmdCopyBuffer(cmd, iStaging, coneIndexBuffer, 1, &copyRegion);
    renderCommandPool.endCommands(cmd);
    
    vkDestroyBuffer(vulkanDevice.getDevice(), vStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), vStagingMem, nullptr);
    vkDestroyBuffer(vulkanDevice.getDevice(), iStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), iStagingMem, nullptr);
}

void Gizmo::createRingGeometry() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn, err;
    
    if (!tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, "models/gizmo_ring.obj")) {
        throw std::runtime_error("Failed to load gizmo_ring.obj: " + warn + err);
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
    
    VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iSize = sizeof(indices[0]) * indices.size();
    
    VkBuffer vStaging, iStaging;
    VkDeviceMemory vStagingMem, iStagingMem;
    
    createBuffer(vulkanDevice, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 vStaging, vStagingMem);
    createBuffer(vulkanDevice, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 iStaging, iStagingMem);
    
    void* data;
    vkMapMemory(vulkanDevice.getDevice(), vStagingMem, 0, vSize, 0, &data);
    memcpy(data, vertices.data(), vSize);
    vkUnmapMemory(vulkanDevice.getDevice(), vStagingMem);
    
    vkMapMemory(vulkanDevice.getDevice(), iStagingMem, 0, iSize, 0, &data);
    memcpy(data, indices.data(), iSize);
    vkUnmapMemory(vulkanDevice.getDevice(), iStagingMem);
    
    createBuffer(vulkanDevice, vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ringVertexBuffer, ringVertexBufferMemory);
    createBuffer(vulkanDevice, iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ringIndexBuffer, ringIndexBufferMemory);
    
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = vSize;
    vkCmdCopyBuffer(cmd, vStaging, ringVertexBuffer, 1, &copyRegion);
    copyRegion.size = iSize;
    vkCmdCopyBuffer(cmd, iStaging, ringIndexBuffer, 1, &copyRegion);
    renderCommandPool.endCommands(cmd);
    
    vkDestroyBuffer(vulkanDevice.getDevice(), vStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), vStagingMem, nullptr);
    vkDestroyBuffer(vulkanDevice.getDevice(), iStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), iStagingMem, nullptr);
}

void Gizmo::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
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
    
    auto bindingDesc = GizmoVertex::getBindingDescription();
    auto attrDescs = GizmoVertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport{0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
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
    
    // Enable stencil writing for gizmo picking
    depthStencil.stencilTestEnable = VK_TRUE;
    
    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_REPLACE;         // Replace stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;            // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;       // Keep stencil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;       // Always pass 
    stencilOp.compareMask = 0xFF;                     // Compare all bits
    stencilOp.writeMask = 0xFF;                       // Write all bits
    stencilOp.reference = 3;                          // Default
    
    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;
    
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttachment.blendEnable = VK_TRUE;  
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;  
    colorBlending.pAttachments = &colorAttachment;
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(GizmoPushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo pipeline layout");
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
    pipelineInfo.subpass = 2;  // Grid subpass
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo pipeline");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

float Gizmo::applyFovScaling(float baseScale) const {
    float currentFov = camera.getFov();
    
    const float BASE_FOV = 15.0f;
    float fovScale = currentFov / BASE_FOV;
    
    fovScale = glm::clamp(fovScale, 0.05f, 2.0f);
    
    return baseScale * fovScale;
}

float Gizmo::getArrowSize(float baseScale) const {
    const float ARROW_SIZE_MULTIPLIER = 0.2f;
    // Apply FOV scaling to arrow size
    float fovScaled = applyFovScaling(baseScale);
    return fovScaled * ARROW_SIZE_MULTIPLIER;
}

float Gizmo::getArrowDistance(float baseScale) const {
    const float DISTANCE_MULTIPLIER = 0.5f;
    // Apply FOV scaling to arrow distance
    float fovScaled = applyFovScaling(baseScale);
    return fovScaled * DISTANCE_MULTIPLIER;
}

void Gizmo::render(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, float scale) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    float finalScale = scale;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // If dragging, only render the active axis in the current mode
    bool isDragging = (activeAxis != GizmoAxis::None);
    
    // Show if not dragging or if translating on this axis
    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::X)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 3);
        renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(1, 0, 0), glm::vec3(0.9, 0.0, 0.05), finalScale, false);
    }
    
    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::Y)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 4);
        renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(0, 1, 0), glm::vec3(0.05, 0.9, 0), finalScale, false);
    }
    
    if (!isDragging || (currentMode == GizmoMode::Translate && activeAxis == GizmoAxis::Z)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 5);
        renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(0, 0, 1), glm::vec3(0.0, 0.05, 0.9), finalScale, false);
    }
    
    // Show if not dragging or if rotating on this axis
    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::X)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 6);
        renderRotationRing(commandBuffer, currentFrame, position, extent, glm::vec3(1, 0, 0), glm::vec3(0.9, 0.0, 0.05), finalScale, false);
    }
    
    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::Y)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 7);
        renderRotationRing(commandBuffer, currentFrame, position, extent, glm::vec3(0, 1, 0), glm::vec3(0.05, 0.9, 0), finalScale, false);
    }
    
    if (!isDragging || (currentMode == GizmoMode::Rotate && activeAxis == GizmoAxis::Z)) {
        vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 8);
        renderRotationRing(commandBuffer, currentFrame, position, extent, glm::vec3(0, 0, 1), glm::vec3(0.0, 0.05, 0.9), finalScale, false);
    }
}

void Gizmo::renderAxis(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, const glm::vec3& direction, const glm::vec3& color, float scale, bool hovered) {
    float aspectRatio = (float)extent.width / (float)extent.height;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
    proj[1][1] *= -1; // Vulkan Y-axis flip
    
    float offsetDistance = getArrowDistance(scale);
    float arrowScale = getArrowSize(scale);

    glm::vec3 offsetPosition = position + direction * offsetDistance; 
    
    glm::mat4 rotation = glm::mat4(1.0f);
    if (direction.x > 0.5f) {
        // X-axis: rotate -90 around Z to point right
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1));
    } else if (direction.x < -0.5f) {
        // -X-axis: rotate 90 around Z
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    } else if (direction.y > 0.5f) {
        // Y-axis: already points up
        rotation = glm::mat4(1.0f);
    } else if (direction.y < -0.5f) {
        // -Y-axis: rotate 180
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0));
    } else if (direction.z > 0.5f) {
        // Z-axis: rotate 90 around X to point forward 
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    } else if (direction.z < -0.5f) {
        // -Z-axis: rotate 90 around X
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    }
    
    glm::mat4 model = glm::translate(glm::mat4(1.0f), offsetPosition) * rotation * glm::scale(glm::mat4(1.0f), glm::vec3(arrowScale));
    
    GizmoPushConstants pc;
    pc.model = model;
    pc.view = view;
    pc.proj = proj;
    pc.color = color;
    pc.hovered = hovered ? 1.0f : 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                       0, sizeof(GizmoPushConstants), &pc);
    
    VkDeviceSize offsets[] = {0};
    VkBuffer coneBuffers[] = {coneVertexBuffer};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, coneBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, coneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, coneIndexCount, 1, 0, 0, 0);
}

void Gizmo::renderRotationRing(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, const glm::vec3& axis, const glm::vec3& color, float scale, bool hovered) {
    float aspectRatio = (float)extent.width / (float)extent.height;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
    proj[1][1] *= -1;
    
    // Apply FOV scaling to ring size
    float fovScaled = applyFovScaling(scale);
    float ringScale = fovScaled * 0.75f; 
    
    // Ring model lies in XY plane
    glm::mat4 rotation = glm::mat4(1.0f);
    if (axis.x > 0.5f) {
        // X-axis ring: rotate 90 deg around Z to get YZ plane
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    } else if (axis.y > 0.5f) {
        // Y-axis ring: stay in XY plane
        rotation = glm::mat4(1.0f);
    } else if (axis.z > 0.5f) {
        // Z-axis ring: rotate 90 deg around X to get XZ plane
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    }
    
    glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(ringScale));
    glm::mat4 translateMatrix = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 model = translateMatrix * rotation * scaleMatrix;
    
    GizmoPushConstants pc;
    pc.model = model;
    pc.view = view;
    pc.proj = proj;
    pc.color = color;
    pc.hovered = hovered ? 1.0f : 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(GizmoPushConstants), &pc);
    
    VkDeviceSize offsets[] = {0};
    VkBuffer ringBuffers[] = {ringVertexBuffer};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, ringBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, ringIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, ringIndexCount, 1, 0, 0, 0);
}

glm::vec3 Gizmo::calculateTranslationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis) {
    if (axis == GizmoAxis::None) 
        return glm::vec3(0);
    
    // Get axis direction
    glm::vec3 axisDir;
    if (axis == GizmoAxis::X) axisDir = glm::vec3(1, 0, 0);
    else if (axis == GizmoAxis::Y) axisDir = glm::vec3(0, 1, 0);
    else if (axis == GizmoAxis::Z) axisDir = glm::vec3(0, 0, 1);
    else return glm::vec3(0);
    
    // Find closest point on axis line to the current mouse ray
    glm::vec3 w0 = dragStartPos - rayOrigin;
    float a = glm::dot(axisDir, axisDir);  
    float b = glm::dot(axisDir, rayDir);
    float c = glm::dot(rayDir, rayDir);
    float d = glm::dot(axisDir, w0);
    float e = glm::dot(rayDir, w0);
    
    float denom = a * c - b * b;
    if (fabs(denom) < 0.0001f) 
        return glm::vec3(0);  // Parallel, no translation
    
    float sc = (b * e - c * d) / denom;
    
    // Current intersection point on axis
    glm::vec3 currentIntersection = dragStartPos + axisDir * sc;
    
    // Difference between current and initial intersection
    glm::vec3 translation = currentIntersection - dragStartIntersection;
    
    // Project onto axis to keep it constrained
    float distance = glm::dot(translation, axisDir);
    return axisDir * distance;
}

void Gizmo::startDrag(GizmoAxis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition) {
    activeAxis = axis;
    dragStartPos = gizmoPosition;
    dragStartRayOrigin = rayOrigin;
    dragStartRayDir = rayDir;
    
    // Calculate where on the axis the initial click intersected
    glm::vec3 axisDir;
    if (axis == GizmoAxis::X) axisDir = glm::vec3(1, 0, 0);
    else if (axis == GizmoAxis::Y) axisDir = glm::vec3(0, 1, 0);
    else if (axis == GizmoAxis::Z) axisDir = glm::vec3(0, 0, 1);
    else axisDir = glm::vec3(0, 0, 0);
    
    // Find closest point on axis line to the initial ray
    glm::vec3 w0 = gizmoPosition - rayOrigin;
    float a = glm::dot(axisDir, axisDir); 
    float b = glm::dot(axisDir, rayDir);
    float c = glm::dot(rayDir, rayDir);
    float d = glm::dot(axisDir, w0);
    float e = glm::dot(rayDir, w0);
    
    float denom = a * c - b * b;
    float sc = (b * e - c * d) / denom;
    
    dragStartIntersection = gizmoPosition + axisDir * sc;
}

void Gizmo::endDrag() {
    activeAxis = GizmoAxis::None;
}

float Gizmo::calculateRotationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis) {
    if (axis == GizmoAxis::None) 
        return 0.0f;
    
    // Get rotation axis
    glm::vec3 axisDir;
    if (axis == GizmoAxis::X) axisDir = glm::vec3(1, 0, 0);
    else if (axis == GizmoAxis::Y) axisDir = glm::vec3(0, 1, 0);
    else if (axis == GizmoAxis::Z) axisDir = glm::vec3(0, 0, 1);
    else return 0.0f;
    
    // Project rays onto plane perpendicular to rotation axis
    glm::vec3 toGizmo = gizmoPosition - rayOrigin;
    float distAlongAxis = glm::dot(toGizmo, axisDir);
    glm::vec3 planeOrigin = rayOrigin + axisDir * distAlongAxis;
    
    // Find where current ray intersects the rotation plane
    float t = glm::dot(gizmoPosition - rayOrigin, axisDir) / glm::dot(rayDir, axisDir);
    if (fabs(glm::dot(rayDir, axisDir)) < 0.0001f) 
        return 0.0f;
    
    glm::vec3 currentPoint = rayOrigin + rayDir * t;
    
    // Find where start ray intersected the plane
    float t0 = glm::dot(gizmoPosition - dragStartRayOrigin, axisDir) / glm::dot(dragStartRayDir, axisDir);
    glm::vec3 startPoint = dragStartRayOrigin + dragStartRayDir * t0;
    
    // Calculate vectors from gizmo center to intersection points
    glm::vec3 startVec = glm::normalize(startPoint - gizmoPosition);
    glm::vec3 currentVec = glm::normalize(currentPoint - gizmoPosition);
    
    // Calculate angle between vectors
    float cosAngle = glm::clamp(glm::dot(startVec, currentVec), -1.0f, 1.0f);
    float angle = acos(cosAngle);
    
    // Determine sign using cross product
    glm::vec3 cross = glm::cross(startVec, currentVec);
    if (glm::dot(cross, axisDir) < 0.0f) {
        angle = -angle;
    }
    
    return glm::degrees(angle);
}

glm::vec3 Gizmo::calculateGizmoPosition(ResourceManager& resourceManager, const ModelSelection& modelSelection) {
    const auto& selectedModelIDs = modelSelection.getSelectedModelIDsRenderThread();
    
    if (selectedModelIDs.empty()) {
        return glm::vec3(0.0f);
    }
    
    glm::vec3 gizmoPosition(0.0f);
    int count = 0;
    
    for (uint32_t id : selectedModelIDs) {
        if (id == 1) {
            // Transform bounding box center to world space
            glm::vec3 localCenter = resourceManager.getVisModel().getBoundingBoxCenter();
            glm::vec3 worldCenter = glm::vec3(resourceManager.getVisModel().getModelMatrix() * glm::vec4(localCenter, 1.0f));
            gizmoPosition += worldCenter;
            count++;
        } else if (id == 2) {
            // Transform bounding box center to world space
            glm::vec3 localCenter = resourceManager.getHeatModel().getBoundingBoxCenter();
            glm::vec3 worldCenter = glm::vec3(resourceManager.getHeatModel().getModelMatrix() * glm::vec4(localCenter, 1.0f));
            gizmoPosition += worldCenter;
            count++;
        }
    }
    
    if (count > 0) {
        gizmoPosition /= static_cast<float>(count);
    } else {
        // Fallback if no valid IDs
        glm::vec3 localCenter = resourceManager.getVisModel().getBoundingBoxCenter();
        gizmoPosition = glm::vec3(resourceManager.getVisModel().getModelMatrix() * glm::vec4(localCenter, 1.0f));
    }
    
    return gizmoPosition;
}

float Gizmo::calculateGizmoScale(ResourceManager& resourceManager, const ModelSelection& modelSelection) {
    const auto& selectedModelIDs = modelSelection.getSelectedModelIDsRenderThread();
    
    float maxBBoxSize = 0.0f;
    
    for (uint32_t id : selectedModelIDs) {
        if (id == 1) {
            glm::vec3 bboxSize = resourceManager.getVisModel().getBoundingBoxMax() - resourceManager.getVisModel().getBoundingBoxMin();
            maxBBoxSize = std::max(maxBBoxSize, std::max(bboxSize.x, std::max(bboxSize.y, bboxSize.z)));
        } else if (id == 2) {
            glm::vec3 bboxSize = resourceManager.getHeatModel().getBoundingBoxMax() - resourceManager.getHeatModel().getBoundingBoxMin();
            maxBBoxSize = std::max(maxBBoxSize, std::max(bboxSize.x, std::max(bboxSize.y, bboxSize.z)));
        }
    }
    
    if (maxBBoxSize == 0.0f) {
        // Fallback if no valid IDs
        glm::vec3 bboxSize = resourceManager.getVisModel().getBoundingBoxMax() - resourceManager.getVisModel().getBoundingBoxMin();
        maxBBoxSize = std::max(bboxSize.x, std::max(bboxSize.y, bboxSize.z));
    }
    
    const float BASE_SCALE_MULTIPLIER = 0.5f;  // gizmo is % of model size
    float gizmoScale = maxBBoxSize * BASE_SCALE_MULTIPLIER;
    
    // Clamp to prevent extreme sizes
    const float MIN_SCALE = 0.1f;
    const float MAX_SCALE = 0.5f;
    gizmoScale = glm::clamp(gizmoScale, MIN_SCALE, MAX_SCALE);
    
    return gizmoScale;
}

void Gizmo::cleanup() {
    if (coneVertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vulkanDevice.getDevice(), coneVertexBuffer, nullptr);
    if (coneVertexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(vulkanDevice.getDevice(), coneVertexBufferMemory, nullptr);
    if (coneIndexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vulkanDevice.getDevice(), coneIndexBuffer, nullptr);
    if (coneIndexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(vulkanDevice.getDevice(), coneIndexBufferMemory, nullptr);
    if (ringVertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vulkanDevice.getDevice(), ringVertexBuffer, nullptr);
    if (ringVertexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(vulkanDevice.getDevice(), ringVertexBufferMemory, nullptr);
    if (ringIndexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vulkanDevice.getDevice(), ringIndexBuffer, nullptr);
    if (ringIndexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(vulkanDevice.getDevice(), ringIndexBufferMemory, nullptr);
    if (pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}
