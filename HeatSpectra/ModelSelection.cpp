#include "ModelSelection.hpp"
#include "VulkanDevice.hpp"
#include "DeferredRenderer.hpp"
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

void ModelSelection::queuePickRequest(int x, int y) {
    std::lock_guard<std::mutex> lock(pickingQueueMutex);
    pickingRequestQueue.push({x, y});
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
        uint8_t stencilID = pickModelAtPosition(request.x, request.y, currentFrame);
        
        // Update selection state
        if (stencilID > 0) {
            setSelected(true);
            setSelectedModelID(stencilID);
            
            if (stencilID == 1) {
                std::cout << "[Selection] VisModel selected (deferred GPU picking)" << std::endl;
            } else if (stencilID == 2) {
                std::cout << "[Selection] HeatModel selected (deferred GPU picking)" << std::endl;
            } else {
                std::cout << "[Selection] Model " << static_cast<int>(stencilID) << " selected (deferred GPU picking)" << std::endl;
            }
        } else {
            setSelected(false);
            setSelectedModelID(0);
            std::cout << "[Selection] Model deselected (deferred GPU picking)" << std::endl;
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

uint8_t ModelSelection::pickModelAtPosition(int x, int y, uint32_t currentFrame) {
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
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
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
    
    // Transition back to read only for next frame 
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
    
    // Read stencil value
    uint8_t stencilValue = *static_cast<uint8_t*>(stagingBufferMapped);
    
    std::cout << "[GPU Picking] Frame=" << currentFrame << " Position=(" << x << ", " << y 
              << ") StencilID=" << static_cast<int>(stencilValue) << std::endl;
    
    return stencilValue;
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
