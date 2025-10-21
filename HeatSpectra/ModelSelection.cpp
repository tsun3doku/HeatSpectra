#include "ModelSelection.hpp"
#include "VulkanDevice.hpp"
#include "DeferredRenderer.hpp"
#include "ResourceManager.hpp"
#include "Model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>

ModelSelection::ModelSelection(VulkanDevice& device, DeferredRenderer& renderer)
    : vulkanDevice(device), deferredRenderer(renderer), pickingCommandPool(VK_NULL_HANDLE),
      stagingBuffer(VK_NULL_HANDLE), stagingBufferMemory(VK_NULL_HANDLE), stagingBufferMapped(nullptr) {
    createPickingCommandPool();
    createStagingBuffer();
}

void ModelSelection::createPickingCommandPool() {
    QueueFamilyIndices indices = vulkanDevice.findQueueFamilies(vulkanDevice.getPhysicalDevice(), vulkanDevice.getSurface());
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = indices.graphicsAndComputeFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // Allow individual command buffer reset
    
    if (vkCreateCommandPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &pickingCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create picking command pool");
    }
}

ModelSelection::~ModelSelection() {
}

bool ModelSelection::getSelected() const {
    return !selectedModelIDs.empty();
}

void ModelSelection::setSelectedModelID(uint32_t id) {
    selectedModelIDs.clear();
    selectedModelIDs.push_back(id);
}

void ModelSelection::addSelectedModelID(uint32_t id) {
    if (std::find(selectedModelIDs.begin(), selectedModelIDs.end(), id) == selectedModelIDs.end()) {
        selectedModelIDs.push_back(id);
    }
}

void ModelSelection::removeSelectedModelID(uint32_t id) {
    auto it = std::find(selectedModelIDs.begin(), selectedModelIDs.end(), id);
    if (it != selectedModelIDs.end()) {
        selectedModelIDs.erase(it);
    }
}

void ModelSelection::clearSelection() {
    selectedModelIDs.clear();
}

bool ModelSelection::isModelSelected(uint32_t id) const {
    return std::find(selectedModelIDs.begin(), selectedModelIDs.end(), id) != selectedModelIDs.end();
}

const std::vector<uint32_t>& ModelSelection::getSelectedModelIDsRenderThread() const {
    return selectedModelIDs;
}

uint32_t ModelSelection::getSelectedModelID() const {
    return selectedModelIDs.empty() ? 0 : selectedModelIDs.front();
}

void ModelSelection::setOutlineColor(const glm::vec3& color) {
    outlineColor = color;
}

glm::vec3 ModelSelection::getOutlineColor() const {
    return outlineColor;
}

void ModelSelection::setOutlineThickness(float thickness) {
    outlineThickness = thickness;
}

float ModelSelection::getOutlineThickness() const {
    return outlineThickness;
}

void ModelSelection::queuePickRequest(int x, int y, bool shiftPressed, float mouseX, float mouseY) {
    PickingRequest request;
    request.x = x;
    request.y = y;
    request.shiftPressed = shiftPressed;
    request.mouseX = mouseX;
    request.mouseY = mouseY;
    
    std::lock_guard<std::mutex> lock(pickingQueueMutex);
    pickingRequestQueue.push(request);
}

void ModelSelection::processPickingRequests(uint32_t currentFrame) {
    if (pickingRequestQueue.empty()) {
        return;  
    }
    
    while (true) {
        PickingRequest request;
        {
            std::lock_guard<std::mutex> lock(pickingQueueMutex);
            if (pickingRequestQueue.empty()) {
                break;
            }
            request = pickingRequestQueue.front();
            pickingRequestQueue.pop();
        }
        
        // Read from the frame
        PickedResult result = pickAtPosition(request.x, request.y, currentFrame);
        lastPickedResult = result;  
        lastPickRequest = request; 
        
        // Debug output
        std::cout << "Picked at (" << request.x << ", " << request.y << "): ";
        if (result.isNone()) std::cout << "Nothing" << std::endl;
        else if (result.isModel()) std::cout << "Model ID " << result.modelID << std::endl;
        else if (result.isGizmo()) std::cout << "Gizmo Axis " << static_cast<int>(result.gizmoAxis) << std::endl;
        
        // Handle selection based on picked result and shift state 
        if (result.isModel()) {
            if (request.shiftPressed) {
                // Shift click on model
                if (isModelSelected(result.modelID)) {
                    removeSelectedModelID(result.modelID);
                } else {
                    addSelectedModelID(result.modelID);
                }
            } else {
                // Regular click on model
                setSelectedModelID(result.modelID);
            }
        } else if (!request.shiftPressed && !result.isGizmo()) {
            // Clicked nothing without shift 
            clearSelection();
        }
    }
}

void ModelSelection::createStagingBuffer() {
    VkDeviceSize bufferSize = 1;  // Single byte for stencil value
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create picking staging buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate picking staging buffer memory");
    }
    
    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingBufferMemory, 0);
    
    // Map persistently
    vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &stagingBufferMapped);
}

PickedResult ModelSelection::pickAtPosition(int x, int y, uint32_t currentFrame) {
    // Create a single time command buffer for the copy
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pickingCommandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // Get the stencil image from DeferredRenderer
    VkImage stencilImage = deferredRenderer.getDepthResolveImages()[currentFrame];
    
    // Transition image to TRANSFER_SRC_OPTIMAL 
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; 
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = stencilImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy single pixel stencil to staging buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {1, 1, 1};
    
    vkCmdCopyImageToBuffer(commandBuffer, stencilImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer, 1, &region);
    
    // Transition back to GENERAL for next frame 
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL; 
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(commandBuffer);
    
    // Submit and wait with a temporary fence 
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence copyFence;
    vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &copyFence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, copyFence);
    vkWaitForFences(vulkanDevice.getDevice(), 1, &copyFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(vulkanDevice.getDevice(), copyFence, nullptr);
    vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
    
    // Read stencil value and decode
    uint8_t stencilValue = *static_cast<uint8_t*>(stagingBufferMapped);
     
    PickedResult result;
     
    // 1-2 = models
    // 3-5 = translation arrows (X, Y, Z)
    // 6-8 = rotation rings (X, Y, Z)
    
    result.stencilValue = stencilValue;
    
    if (stencilValue == 1 || stencilValue == 2) {
        result.type = PickedType::Model;
        result.modelID = stencilValue;
    } else if (stencilValue >= 3 && stencilValue <= 8) {
        result.type = PickedType::Gizmo;
        // Map both translation (3-5) and rotation (6-8) to X/Y/Z
        result.gizmoAxis = static_cast<PickedGizmoAxis>(((stencilValue - 3) % 3) + 1);
    } else {
        result.type = PickedType::None;
    }
    
    return result;
}

PickedResult ModelSelection::pickImmediately(int x, int y, uint32_t currentFrame) {
    return pickAtPosition(x, y, currentFrame);
}

void ModelSelection::cleanup() {
    if (stagingBufferMapped != nullptr) {
        vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);
        stagingBufferMapped = nullptr;
    }
    if (stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
    }
    if (stagingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        stagingBufferMemory = VK_NULL_HANDLE;
    }
    if (pickingCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkanDevice.getDevice(), pickingCommandPool, nullptr);
        pickingCommandPool = VK_NULL_HANDLE;
    }
}
