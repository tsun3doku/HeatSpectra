#include "Gizmo.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp"
#include "Camera.hpp"
#include "File_utils.h"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include <stdexcept>
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

// Helper to create buffer
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

Gizmo::Gizmo(VulkanDevice& device, MemoryAllocator& allocator, Camera& cam,
             VkRenderPass renderPass, VkExtent2D extent, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), camera(cam), renderCommandPool(cmdPool),
      pipeline(VK_NULL_HANDLE), pipelineLayout(VK_NULL_HANDLE),
      arrowVertexBuffer(VK_NULL_HANDLE), arrowVertexBufferMemory(VK_NULL_HANDLE),
      arrowIndexBuffer(VK_NULL_HANDLE), arrowIndexBufferMemory(VK_NULL_HANDLE),
      coneVertexBuffer(VK_NULL_HANDLE), coneVertexBufferMemory(VK_NULL_HANDLE),
      coneIndexBuffer(VK_NULL_HANDLE), coneIndexBufferMemory(VK_NULL_HANDLE),
      currentMode(GizmoMode::Translate), hoveredAxis(GizmoAxis::None), activeAxis(GizmoAxis::None),
      arrowIndexCount(0), coneIndexCount(0) {
    createGeometry();
    createPipeline(renderPass, extent);
}

Gizmo::~Gizmo() {}

void Gizmo::cleanup() {
    if (arrowVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vulkanDevice.getDevice(), arrowVertexBuffer, nullptr);
    if (arrowVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(vulkanDevice.getDevice(), arrowVertexBufferMemory, nullptr);
    if (arrowIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vulkanDevice.getDevice(), arrowIndexBuffer, nullptr);
    if (arrowIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(vulkanDevice.getDevice(), arrowIndexBufferMemory, nullptr);
    if (coneVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vulkanDevice.getDevice(), coneVertexBuffer, nullptr);
    if (coneVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(vulkanDevice.getDevice(), coneVertexBufferMemory, nullptr);
    if (coneIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vulkanDevice.getDevice(), coneIndexBuffer, nullptr);
    if (coneIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(vulkanDevice.getDevice(), coneIndexBufferMemory, nullptr);
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(vulkanDevice.getDevice(), pipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void Gizmo::createGeometry() {
    createArrowGeometry();
    createConeGeometry();
}

void Gizmo::createArrowGeometry() {
    const int segments = 16;
    const float radius = 0.02f;
    const float length = 1.0f;
    
    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;
    
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        vertices.push_back({{x, 0.0f, z}, {1.0f, 1.0f, 1.0f}});
        vertices.push_back({{x, length, z}, {1.0f, 1.0f, 1.0f}});
    }
    
    for (int i = 0; i < segments; ++i) {
        int base = i * 2;
        indices.push_back(base); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 1); indices.push_back(base + 2); indices.push_back(base + 3);
    }
    
    arrowIndexCount = static_cast<uint32_t>(indices.size());
    
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
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, arrowVertexBuffer, arrowVertexBufferMemory);
    createBuffer(vulkanDevice, iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, arrowIndexBuffer, arrowIndexBufferMemory);
    
    // Use render command pool for initialization
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = vSize;
    vkCmdCopyBuffer(cmd, vStaging, arrowVertexBuffer, 1, &copyRegion);
    copyRegion.size = iSize;
    vkCmdCopyBuffer(cmd, iStaging, arrowIndexBuffer, 1, &copyRegion);
    renderCommandPool.endCommands(cmd);
    
    vkDestroyBuffer(vulkanDevice.getDevice(), vStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), vStagingMem, nullptr);
    vkDestroyBuffer(vulkanDevice.getDevice(), iStaging, nullptr);
    vkFreeMemory(vulkanDevice.getDevice(), iStagingMem, nullptr);
}

void Gizmo::createConeGeometry() {
    const int segments = 16;
    const float radius = 0.08f;
    const float height = 0.3f;
    
    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;
    
    vertices.push_back({{0.0f, height, 0.0f}, {1.0f, 1.0f, 1.0f}});
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        vertices.push_back({{radius * cos(theta), 0.0f, radius * sin(theta)}, {1.0f, 1.0f, 1.0f}});
    }
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0); indices.push_back(i + 1); indices.push_back(i + 2);
    }
    int centerIdx = segments + 2;
    for (int i = 0; i < segments; ++i) {
        indices.push_back(centerIdx); indices.push_back(i + 2); indices.push_back(i + 1);
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
    
    // Use render command pool for initialization
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
    
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttachment.blendEnable = VK_TRUE;
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    
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
    
    // Enable dynamic state for viewport and scissor to handle window resize
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
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
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo pipeline");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void Gizmo::render(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, float scale) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    // Set viewport and scissor dynamically to handle window resize
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
    
    renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(1, 0, 0), glm::vec3(1, 0, 0), scale, hoveredAxis == GizmoAxis::X);
    renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), scale, hoveredAxis == GizmoAxis::Y);
    renderAxis(commandBuffer, currentFrame, position, extent, glm::vec3(0, 0, 1), glm::vec3(0, 0, 1), scale, hoveredAxis == GizmoAxis::Z);
}

void Gizmo::renderAxis(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position,
                       VkExtent2D extent, const glm::vec3& direction, const glm::vec3& color, float scale, bool hovered) {
    float aspectRatio = (float)extent.width / (float)extent.height;
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
    proj[1][1] *= -1; // Vulkan Y-axis flip
    
    // Arrow geometry is modeled along Y-axis (0,1,0)
    // Rotate to align with the desired direction
    glm::mat4 rotation = glm::mat4(1.0f);
    if (direction.x > 0.5f) {
        // X-axis: rotate -90° around Z to point right (negated for left-handed)
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1));
    } else if (direction.x < -0.5f) {
        // -X-axis: rotate 90° around Z
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    } else if (direction.y > 0.5f) {
        // Y-axis: no rotation needed (already points up)
        rotation = glm::mat4(1.0f);
    } else if (direction.y < -0.5f) {
        // -Y-axis: rotate 180°
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0));
    } else if (direction.z > 0.5f) {
        // Z-axis: rotate 90° around X to point forward (negated for left-handed)
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    } else if (direction.z < -0.5f) {
        // -Z-axis: rotate 90° around X
        rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    }
    
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position) * rotation * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    
    GizmoPushConstants pc;
    pc.model = model;
    pc.view = view;
    pc.proj = proj;
    pc.color = color;
    pc.hovered = hovered ? 1.0f : 0.0f;
    
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                       0, sizeof(GizmoPushConstants), &pc);
    
    VkBuffer vertexBuffers[] = {arrowVertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, arrowIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, arrowIndexCount, 1, 0, 0, 0);
    
    glm::mat4 coneModel = glm::translate(model, glm::vec3(0, 1.0f, 0));
    pc.model = coneModel;
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(GizmoPushConstants), &pc);
    
    VkBuffer coneBuffers[] = {coneVertexBuffer};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, coneBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, coneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, coneIndexCount, 1, 0, 0, 0);
}

bool Gizmo::rayIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition,
                         float gizmoScale, GizmoAxis& hitAxis, float& hitDistance) {
    hitAxis = GizmoAxis::None;
    hitDistance = FLT_MAX;
    
    glm::vec3 axes[] = {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)};
    GizmoAxis axisEnums[] = {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    
    for (int i = 0; i < 3; ++i) {
        float dist;
        glm::vec3 start = gizmoPosition;
        glm::vec3 end = gizmoPosition + axes[i] * gizmoScale;
        
        if (rayCylinderIntersect(rayOrigin, rayDir, start, end, 0.02f * gizmoScale, dist)) {
            if (dist < hitDistance) {
                hitDistance = dist;
                hitAxis = axisEnums[i];
            }
        }
    }
    
    return hitAxis != GizmoAxis::None;
}

glm::vec3 Gizmo::calculateTranslationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                            const glm::vec3& gizmoPosition, GizmoAxis axis) {
    if (axis == GizmoAxis::None) return glm::vec3(0);
    
    // Get axis direction
    glm::vec3 axisDir;
    if (axis == GizmoAxis::X) axisDir = glm::vec3(1, 0, 0);
    else if (axis == GizmoAxis::Y) axisDir = glm::vec3(0, 1, 0);
    else if (axis == GizmoAxis::Z) axisDir = glm::vec3(0, 0, 1);
    else return glm::vec3(0);
    
    // Find closest point on axis line to the CURRENT mouse ray
    glm::vec3 w0 = dragStartPos - rayOrigin;
    float a = glm::dot(axisDir, axisDir);  // Always 1 for unit vector
    float b = glm::dot(axisDir, rayDir);
    float c = glm::dot(rayDir, rayDir);
    float d = glm::dot(axisDir, w0);
    float e = glm::dot(rayDir, w0);
    
    float denom = a * c - b * b;
    if (fabs(denom) < 0.0001f) return glm::vec3(0);  // Parallel, no translation
    
    float sc = (b * e - c * d) / denom;
    
    // Current intersection point on axis
    glm::vec3 currentIntersection = dragStartPos + axisDir * sc;
    
    // Calculate translation: difference between current and initial intersection
    glm::vec3 translation = currentIntersection - dragStartIntersection;
    
    // Project onto axis to ensure it's constrained
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
    float a = glm::dot(axisDir, axisDir);  // Always 1 for unit vector
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

bool Gizmo::rayCylinderIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                  const glm::vec3& p1, const glm::vec3& p2, float radius, float& distance) {
    glm::vec3 d = p2 - p1;
    glm::vec3 m = rayOrigin - p1;
    glm::vec3 n = rayDir;
    
    float md = glm::dot(m, d);
    float nd = glm::dot(n, d);
    float dd = glm::dot(d, d);
    
    float nn = glm::dot(n, n);
    float mn = glm::dot(m, n);
    float mm = glm::dot(m, m);
    
    float a = dd * nn - nd * nd;
    float k = mm - radius * radius;
    float c = dd * k - md * md;
    
    if (fabs(a) < 0.0001f) return false;
    
    float b = dd * mn - nd * md;
    float discr = b * b - a * c;
    
    if (discr < 0.0f) return false;
    
    float t = (-b - sqrt(discr)) / a;
    if (t < 0.0f) return false;
    
    float s = md + t * nd;
    if (s < 0.0f || s > dd) return false;
    
    distance = t;
    return true;
}

bool Gizmo::rayConeIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                              const glm::vec3& coneBase, const glm::vec3& coneAxis,
                              float height, float radius, float& distance) {
    // Simplified cone intersection
    return false;
}
