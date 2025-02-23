#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Model;
class Grid;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;

const std::vector<float> clearColorValues = { {0.012f, 0.014f, 0.015f, 1.0f} };

class GBuffer {
public:
    GBuffer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat);
    ~GBuffer();
    void init(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, HeatSystem& heatSystem,
        uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight);
    void createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat);
    void createFramebuffers(const VulkanDevice& vulkanDevice, std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight);
    void updateDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    void createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight);

    void createGeometryDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGeometryDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGeometryDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight);

    void createLightingDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createLightingDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createLightingDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight);

    void createGeometryPipeline(const VulkanDevice& vulkanDevice, VkExtent2D extent);
    void createLightingPipeline(const VulkanDevice& vulkanDevice, VkExtent2D swapchainExtent);

    void createCommandBuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void freeCommandBuffers(VulkanDevice& vulkanDevice);
    void recordCommandBuffer(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, std::vector<VkImageView> swapChainImageViews, 
        uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent);

    void cleanupFramebuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    // Getters 
    std::vector<VkImageView>& getAlbedoImageViews() {
        return gAlbedoImageViews;
    }
    std::vector<VkImageView>& getNormalImageViews() {
        return gNormalImageViews;
    }
    std::vector<VkImageView>& getPositionImageViews() {
        return gPositionImageViews;
    }
    std::vector<VkImageView>& getDepthImageViews() {
        return gDepthImageViews;
    }

    std::vector<VkImage>& getAlbedoImages() {
        return gAlbedoImages;
    }
    std::vector<VkImage>& getNormalImages() {
        return gNormalImages;
    }
    std::vector<VkImage>& getPositionImages() {
        return gPositionImages;
    }
    std::vector<VkImage>& getDepthImages() {
        return gDepthImages;
    }

    VkRenderPass& getRenderPass() {
        return renderPass;
    }
    std::vector<VkFramebuffer>& getFramebuffers() {
        return framebuffers;
    }

    VkPipeline& getGbufferPipeline() {
        return geometryPipeline;
    }

    VkPipelineLayout& getGbufferPipelineLayout() {
        return geometryPipelineLayout;
    }

    std::vector<VkCommandBuffer>& getCommandBuffers() {
        return gbufferCommandBuffers;
    }

private:
    const VulkanDevice* vulkanDevice = nullptr;
    MemoryAllocator* memoryAllocator = nullptr;
    ResourceManager* resourceManager;
    
    HeatSystem* heatSystem = nullptr;
   
    uint32_t currentFrame = 0;

    VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);
    bool hasStencilComponent(VkFormat format);

    std::vector<VkCommandBuffer> gbufferCommandBuffers;

    std::vector<VkImage> gAlbedoImages;
    std::vector<VkImage> gNormalImages;
    std::vector<VkImage> gPositionImages;
    std::vector<VkImage> gDepthImages;

    std::vector<VkImageView> gAlbedoImageViews;
    std::vector<VkImageView> gNormalImageViews;
    std::vector<VkImageView> gPositionImageViews;
    std::vector<VkImageView> gDepthImageViews;

    std::vector<VkDeviceMemory> gAlbedoImageMemories;
    std::vector<VkDeviceMemory> gNormalImageMemories;
    std::vector<VkDeviceMemory> gPositionImageMemories;
    std::vector<VkDeviceMemory> gDepthImageMemories;

    VkImageCreateInfo gAlbedoImageInfo;
    VkImageCreateInfo gNormalImageInfo;
    VkImageCreateInfo gPositionImageInfo;
    VkImageCreateInfo gDepthImageInfo;

    VkRenderPass renderPass;
    VkRenderPassCreateInfo renderPassInfo;
    std::vector<VkFramebuffer> framebuffers;

    VkDescriptorPool geometryDescriptorPool;
    VkDescriptorSetLayout geometryDescriptorSetLayout;
    std::vector<VkDescriptorSet> geometryDescriptorSets;

    VkDescriptorPool lightingDescriptorPool;
    VkDescriptorSetLayout lightingDescriptorSetLayout;
    std::vector<VkDescriptorSet> lightingDescriptorSets;

    VkPipelineLayout geometryPipelineLayout;
    VkPipeline geometryPipeline;

    VkPipelineLayout lightingPipelineLayout;
    VkPipeline lightingPipeline;

};