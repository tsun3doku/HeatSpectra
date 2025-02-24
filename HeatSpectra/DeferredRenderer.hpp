#pragma once

class GBuffer;
class VulkanDevice;

class DeferredRenderer {
public:
    DeferredRenderer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight);
    ~DeferredRenderer();

    void createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat);
    void createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight);
    void cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void cleanup(VulkanDevice& vulkanDevice);

    // Getters
    const VkRenderPass& getRenderPass() const {
        return renderPass; 
    }
    const std::vector<VkImage>& getAlbedoImages() const {
        return albedoImages;
    }
    const std::vector<VkImage>& getNormalImages() const {
        return normalImages;
    }
    const std::vector<VkImage>& getPositionImages() const {
        return positionImages;
    }
    const std::vector<VkImage>& getDepthImages() const { 
        return depthImages; 
    }

    const std::vector<VkImageView>& getAlbedoViews() const {
        return albedoViews;
    }
    const std::vector<VkImageView>& getNormalViews() const { 
        return normalViews; 
    }
    const std::vector<VkImageView>& getPositionViews() const { 
        return positionViews; 
    }
    const std::vector<VkImageView>& getDepthViews() const { 
        return depthViews; 
    }

private:
    VulkanDevice& vulkanDevice;
    VkRenderPass renderPass;

    std::vector<VkImage> albedoImages, normalImages, positionImages, depthImages;
    std::vector<VkDeviceMemory> albedoMemories, normalMemories, positionMemories, depthMemories;
    std::vector<VkImageView> albedoViews, normalViews, positionViews, depthViews;
    VkImageCreateInfo albedoImageInfo, normalImageInfo, positionImageInfo, depthImageInfo;
};  