#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class GBuffer;
class VulkanDevice;

class DeferredRenderer {
public:
    DeferredRenderer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight);
    ~DeferredRenderer();

    void createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat);
    void createImageViews(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D extent, uint32_t maxFramesInFlight);
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
    const std::vector<VkImage>& getGridImages() const {
        return gridImages;
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
    const std::vector<VkImageView>& getGridViews() const {
        return gridViews;
    }

    const std::vector<VkImageView>& getAlbedoResolveViews() const {
        return albedoResolveViews;
    }
    const std::vector<VkImageView>& getNormalResolveViews() const {
        return normalResolveViews;
    }
    const std::vector<VkImageView>& getPositionResolveViews() const {
        return positionResolveViews;
    }
    const std::vector<VkImageView>& getDepthResolveViews() const {
        return depthResolveViews;
    }
    const std::vector<VkImageView>& getGridResolveViews() const {
        return gridResolveViews;
    }

private:
    VulkanDevice& vulkanDevice;
    VkRenderPass renderPass;

    std::vector<VkImage> albedoImages, normalImages, positionImages, depthImages, gridImages;
    std::vector<VkDeviceMemory> albedoMemories, normalMemories, positionMemories, depthMemories, gridMemories;
    std::vector<VkImageView> albedoViews, normalViews, positionViews, depthViews, gridViews;
    VkImageCreateInfo albedoImageInfo, normalImageInfo, positionImageInfo, depthImageInfo, gridImageInfo;

    std::vector<VkImageView> albedoResolveViews, normalResolveViews, positionResolveViews, depthResolveViews, gridResolveViews;
    std::vector<VkImage> albedoResolveImages, normalResolveImages, positionResolveImages, depthResolveImages, gridResolveImages;
    std::vector<VkDeviceMemory> albedoResolveMemories, normalResolveMemories, positionResolveMemories, depthResolveMemories, gridResolveMemories;
};  