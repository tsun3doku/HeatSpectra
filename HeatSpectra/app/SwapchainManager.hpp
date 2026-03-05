#pragma once

#include <vulkan/vulkan.h>

#include <vector>

class VulkanDevice;
struct WindowRuntimeState;

class SwapchainManager {
public:
    void initialize(VulkanDevice& vulkanDevice, const WindowRuntimeState& windowState);
    bool create();
    void cleanup();

    VkSwapchainKHR getSwapChain() const {
        return swapChain;
    }

    const std::vector<VkImage>& getImages() const {
        return images;
    }

    const std::vector<VkImageView>& getImageViews() const {
        return imageViews;
    }

    VkFormat getImageFormat() const {
        return imageFormat;
    }

    VkExtent2D getExtent() const {
        return extent;
    }

private:
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    bool hasValidExtent(const VkExtent2D& extent, const VkSurfaceCapabilitiesKHR& capabilities) const;

    VulkanDevice* vulkanDevice = nullptr;
    const WindowRuntimeState* windowState = nullptr;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};
