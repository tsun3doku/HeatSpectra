#include "ModelSelection.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

ModelSelection::ModelSelection(
    VulkanDevice& device,
    VkFrameGraphRuntime& runtime,
    ModelRegistry& resourceManager,
    framegraph::ResourceId depthResolveResourceId)
    : vulkanDevice(device), frameGraphRuntime(runtime), resourceManager(resourceManager), depthResolveResourceId(depthResolveResourceId), pickingCommandPool(VK_NULL_HANDLE),
      stagingBuffer(VK_NULL_HANDLE), stagingBufferMemory(VK_NULL_HANDLE), stagingBufferMapped(nullptr) {
    if (!createPickingCommandPool() || !createStagingBuffer()) {
        std::cerr << "[ModelSelection] Initialization failed" << std::endl;
        cleanup();
        return;
    }
    initialized = true;
}

bool ModelSelection::createPickingCommandPool() {
    QueueFamilyIndices indices = vulkanDevice.getQueueFamilyIndices();
    if (!indices.graphicsAndComputeFamily.has_value()) {
        std::cerr << "[ModelSelection] Graphics/compute queue family is not initialized" << std::endl;
        return false;
    }
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = indices.graphicsAndComputeFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  
    
    if (vkCreateCommandPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &pickingCommandPool) != VK_SUCCESS) {
        std::cerr << "[ModelSelection] Failed to create picking command pool" << std::endl;
        return false;
    }
    return true;
}

ModelSelection::~ModelSelection() {
    cleanup();
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

const std::vector<uint32_t>& ModelSelection::getSelectedRuntimeModelIDsRenderThread() const {
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
    if (!initialized) {
        return;
    }
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

bool ModelSelection::createStagingBuffer() {
    VkDeviceSize bufferSize = 1;  
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        std::cerr << "[ModelSelection] Failed to create picking staging buffer" << std::endl;
        return false;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    uint32_t memoryTypeIndex = UINT32_MAX;
    if (!vulkanDevice.findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            memoryTypeIndex)) {
        std::cerr << "[ModelSelection] Failed to find host visible memory type for picking staging buffer" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        return false;
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        std::cerr << "[ModelSelection] Failed to allocate picking staging buffer memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        return false;
    }
    
    if (vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingBufferMemory, 0) != VK_SUCCESS) {
        std::cerr << "[ModelSelection] Failed to bind picking staging buffer memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        stagingBufferMemory = VK_NULL_HANDLE;
        return false;
    }
    
    // Map persistently
    if (vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &stagingBufferMapped) != VK_SUCCESS || !stagingBufferMapped) {
        std::cerr << "[ModelSelection] Failed to map picking staging buffer memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);
        stagingBufferMemory = VK_NULL_HANDLE;
        stagingBufferMapped = nullptr;
        return false;
    }
    return true;
}

PickedResult ModelSelection::pickAtPosition(int x, int y, uint32_t currentFrame) {
    if (!initialized || pickingCommandPool == VK_NULL_HANDLE || stagingBuffer == VK_NULL_HANDLE || stagingBufferMapped == nullptr) {
        return PickedResult();
    }
    if (x < 0 || y < 0) {
        return PickedResult();
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pickingCommandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS || commandBuffer == VK_NULL_HANDLE) {
        return PickedResult();
    }
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    
    // Get the stencil image from frameGraph
    const auto& depthResolveImages = frameGraphRuntime.getResourceImages(depthResolveResourceId);
    if (currentFrame >= depthResolveImages.size()) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    VkImage stencilImage = depthResolveImages[currentFrame];
    if (stencilImage == VK_NULL_HANDLE) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    
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
    
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence copyFence;
    if (vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &copyFence) != VK_SUCCESS || copyFence == VK_NULL_HANDLE) {
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    const VkResult submitResult = vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, copyFence);
    if (submitResult != VK_SUCCESS) {
        vkDestroyFence(vulkanDevice.getDevice(), copyFence, nullptr);
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    if (vkWaitForFences(vulkanDevice.getDevice(), 1, &copyFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkDestroyFence(vulkanDevice.getDevice(), copyFence, nullptr);
        vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
        return PickedResult();
    }
    vkDestroyFence(vulkanDevice.getDevice(), copyFence, nullptr);
    vkFreeCommandBuffers(vulkanDevice.getDevice(), pickingCommandPool, 1, &commandBuffer);
    
    uint8_t stencilValue = *static_cast<uint8_t*>(stagingBufferMapped);
     
    PickedResult result;
     
    result.stencilValue = stencilValue;

    // Model IDs are encoded directly into stencil values.
    // 3-5 = translation arrows (X, Y, Z)
    // 6-8 = rotation rings (X, Y, Z)  

    if (stencilValue >= 3 && stencilValue <= 8) {
        result.type = PickedType::Gizmo;
        result.gizmoAxis = static_cast<PickedGizmoAxis>(((stencilValue - 3) % 3) + 1);
    } else if (resourceManager.hasModel(stencilValue)) {
        result.type = PickedType::Model;
        result.modelID = stencilValue;
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
    initialized = false;
}


