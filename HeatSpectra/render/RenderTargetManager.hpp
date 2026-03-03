#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct WindowRuntimeState;
class VulkanDevice;

class RenderTargetManager {
public:
    void initialize(VulkanDevice& vulkanDevice, const WindowRuntimeState& windowState);
    bool create();
    void cleanup();

    VkFormat getImageFormat() const {
        return imageFormat;
    }

    VkExtent2D getExtent() const {
        return extent;
    }

    const std::vector<VkImage>& getImages() const {
        return images;
    }

    const std::vector<VkImageView>& getImageViews() const {
        return imageViews;
    }

    uint64_t getGeneration() const {
        return generation;
    }

private:
    VkExtent2D chooseExtent() const;
    bool hasValidExtent(const VkExtent2D& extent) const;

    VulkanDevice* vulkanDevice = nullptr;
    const WindowRuntimeState* windowState = nullptr;

    std::vector<VkDeviceMemory> imageMemories;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkFormat imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    uint64_t generation = 0;
};

