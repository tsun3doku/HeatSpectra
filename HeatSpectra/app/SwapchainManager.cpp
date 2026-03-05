#include "SwapchainManager.hpp"

#include "render/WindowRuntimeState.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

#include <algorithm>
#include <limits>

void SwapchainManager::initialize(VulkanDevice& vulkanDevice, const WindowRuntimeState& windowState) {
    this->vulkanDevice = &vulkanDevice;
    this->windowState = &windowState;
}

bool SwapchainManager::create() {
    if (!vulkanDevice || !windowState || vulkanDevice->getSurface() == VK_NULL_HANDLE) {
        return false;
    }

    cleanup();

    const SwapChainSupportDetails swapChainSupport =
        vulkanDevice->querySwapChainSupport(vulkanDevice->getPhysicalDevice(), vulkanDevice->getSurface());

    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty()) {
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(swapChainSupport.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(swapChainSupport.presentModes);
    const VkExtent2D swapChainExtent = chooseExtent(swapChainSupport.capabilities);

    if (!hasValidExtent(swapChainExtent, swapChainSupport.capabilities)) {
        return false;
    }

    uint32_t imageCount = 2;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    if (imageCount < swapChainSupport.capabilities.minImageCount) {
        imageCount = swapChainSupport.capabilities.minImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vulkanDevice->getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapChainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilyIndices indices =
        vulkanDevice->findQueueFamilies(vulkanDevice->getPhysicalDevice(), vulkanDevice->getSurface());
    const uint32_t queueFamilyIndices[] = {
        indices.graphicsAndComputeFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsAndComputeFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(vulkanDevice->getDevice(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        return false;
    }

    vkGetSwapchainImagesKHR(vulkanDevice->getDevice(), swapChain, &imageCount, nullptr);
    images.resize(imageCount);
    vkGetSwapchainImagesKHR(vulkanDevice->getDevice(), swapChain, &imageCount, images.data());

    imageFormat = surfaceFormat.format;
    extent = swapChainExtent;
    imageViews.resize(images.size(), VK_NULL_HANDLE);
    for (uint32_t i = 0; i < static_cast<uint32_t>(images.size()); ++i) {
        if (createImageView(*vulkanDevice, images[i], imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, imageViews[i]) != VK_SUCCESS) {
            cleanup();
            return false;
        }
    }
    return true;
}

void SwapchainManager::cleanup() {
    if (vulkanDevice && vulkanDevice->getDevice() != VK_NULL_HANDLE) {
        for (VkImageView imageView : imageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(vulkanDevice->getDevice(), imageView, nullptr);
            }
        }
    }
    imageViews.clear();
    images.clear();

    if (swapChain != VK_NULL_HANDLE && vulkanDevice && vulkanDevice->getDevice() != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkanDevice->getDevice(), swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    imageFormat = VK_FORMAT_UNDEFINED;
    extent = {};
}

VkSurfaceFormatKHR SwapchainManager::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const {
    for (const VkSurfaceFormatKHR& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR SwapchainManager::choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const {
    for (const VkPresentModeKHR availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapchainManager::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent{};
    actualExtent.width = windowState->width.load(std::memory_order_acquire);
    actualExtent.height = windowState->height.load(std::memory_order_acquire);
    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
    return actualExtent;
}

bool SwapchainManager::hasValidExtent(const VkExtent2D& extent, const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    if (extent.width < capabilities.minImageExtent.width ||
        extent.height < capabilities.minImageExtent.height) {
        return false;
    }

    if (extent.width > capabilities.maxImageExtent.width ||
        extent.height > capabilities.maxImageExtent.height) {
        return false;
    }

    return true;
}
