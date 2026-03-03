#include "RenderTargetManager.hpp"

#include "RenderConfig.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"
#include "WindowRuntimeState.hpp"

#include <algorithm>
#include <iostream>

void RenderTargetManager::initialize(VulkanDevice& vulkanDevice, const WindowRuntimeState& windowState) {
    this->vulkanDevice = &vulkanDevice;
    this->windowState = &windowState;
}

bool RenderTargetManager::create() {
    if (!vulkanDevice || !windowState) {
        std::cerr << "[RenderTargetManager] Not initialized" << std::endl;
        return false;
    }

    cleanup();

    const VkExtent2D targetExtent = chooseExtent();
    if (!hasValidExtent(targetExtent)) {
        std::cerr << "[RenderTargetManager] Invalid offscreen target extent" << std::endl;
        return false;
    }

    extent = targetExtent;

    const uint32_t imageCount = renderconfig::MaxFramesInFlight;
    images.resize(imageCount, VK_NULL_HANDLE);
    imageMemories.resize(imageCount, VK_NULL_HANDLE);
    imageViews.resize(imageCount, VK_NULL_HANDLE);

    for (uint32_t index = 0; index < imageCount; ++index) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = imageFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(vulkanDevice->getDevice(), &imageInfo, nullptr, &images[index]) != VK_SUCCESS) {
            std::cerr << "[RenderTargetManager] Failed to create offscreen color image" << std::endl;
            cleanup();
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(vulkanDevice->getDevice(), images[index], &memoryRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        uint32_t memoryTypeIndex = 0;
        if (!vulkanDevice->findMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            memoryTypeIndex)) {
            std::cerr << "[RenderTargetManager] Failed to find image memory type" << std::endl;
            cleanup();
            return false;
        }
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(vulkanDevice->getDevice(), &allocInfo, nullptr, &imageMemories[index]) != VK_SUCCESS) {
            std::cerr << "[RenderTargetManager] Failed to allocate offscreen image memory" << std::endl;
            cleanup();
            return false;
        }

        if (vkBindImageMemory(vulkanDevice->getDevice(), images[index], imageMemories[index], 0) != VK_SUCCESS) {
            std::cerr << "[RenderTargetManager] Failed to bind offscreen image memory" << std::endl;
            cleanup();
            return false;
        }

        if (createImageView(
            *vulkanDevice,
            images[index],
            imageFormat,
            VK_IMAGE_ASPECT_COLOR_BIT,
            imageViews[index]) != VK_SUCCESS) {
            std::cerr << "[RenderTargetManager] Failed to create offscreen image view" << std::endl;
            cleanup();
            return false;
        }
    }

    ++generation;
    return true;
}

void RenderTargetManager::cleanup() {
    if (!vulkanDevice) {
        images.clear();
        imageViews.clear();
        imageMemories.clear();
        extent = {};
        return;
    }

    for (VkImageView imageView : imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkanDevice->getDevice(), imageView, nullptr);
        }
    }
    imageViews.clear();

    for (VkImage image : images) {
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(vulkanDevice->getDevice(), image, nullptr);
        }
    }
    images.clear();

    for (VkDeviceMemory memory : imageMemories) {
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->getDevice(), memory, nullptr);
        }
    }
    imageMemories.clear();

    extent = {};
}

VkExtent2D RenderTargetManager::chooseExtent() const {
    VkExtent2D actualExtent{};
    actualExtent.width = windowState->width.load(std::memory_order_acquire);
    actualExtent.height = windowState->height.load(std::memory_order_acquire);

    actualExtent.width = std::max<uint32_t>(actualExtent.width, static_cast<uint32_t>(renderconfig::MinSwapchainExtent));
    actualExtent.height = std::max<uint32_t>(actualExtent.height, static_cast<uint32_t>(renderconfig::MinSwapchainExtent));
    return actualExtent;
}

bool RenderTargetManager::hasValidExtent(const VkExtent2D& extent) const {
    return extent.width > 0 && extent.height > 0;
}

