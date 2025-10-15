#pragma once

class Model;
class Grid;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class ResourceManager;
class DeferredRenderer;
class MemoryAllocator;
class VulkanDevice;

const std::vector<float> clearColorValues = { {0.013f, 0.0138f, 0.0135f, 1.0f} };

class GBuffer {
public:
    GBuffer(VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager,
        uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight, bool drawWireframe);
    ~GBuffer();

    void createFramebuffers(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight);
    void updateDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, uint32_t maxFramesInFlight);

    void createGeometryDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createGeometryDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createGeometryDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createLightingDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createLightingDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createLightingDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createBlendDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createBlendDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createBlendDescriptorSets(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, uint32_t maxFramesInFlight);

    void createGeometryPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent);
    void createWireframePipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent);
    void createLightingPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D swapchainExtent);
    void createIntrinsicOverlayPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent);
    void createBlendPipeline(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, VkExtent2D extent);

    void createCommandBuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void freeCommandBuffers(VulkanDevice& vulkanDevice);
    void recordCommandBuffer(const VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, HeatSystem& heatSystem, std::vector<VkImageView> swapChainImageViews,
        uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent, bool drawWireframe, bool drawCommonSubdivision);

    void cleanupFramebuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    // Getters 
    const std::vector<VkFramebuffer>& getFramebuffers() const {
        return framebuffers;
    }

    VkPipeline& getGbufferPipeline() {
        return geometryPipeline;
    }

    VkPipelineLayout& getGbufferPipelineLayout() {
        return geometryPipelineLayout;
    }

    const std::vector<VkCommandBuffer>& getCommandBuffers() const {
        return gbufferCommandBuffers;
    }

private:
    VulkanDevice& vulkanDevice;

    uint32_t currentFrame = 0;

    VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);
    bool hasStencilComponent(VkFormat format);

    std::vector<VkCommandBuffer> gbufferCommandBuffers;

    std::vector<VkFramebuffer> framebuffers;

    VkDescriptorPool geometryDescriptorPool;
    VkDescriptorSetLayout geometryDescriptorSetLayout;
    std::vector<VkDescriptorSet> geometryDescriptorSets;

    VkDescriptorPool lightingDescriptorPool;
    VkDescriptorSetLayout lightingDescriptorSetLayout;
    std::vector<VkDescriptorSet> lightingDescriptorSets;

    VkDescriptorPool blendDescriptorPool;
    VkDescriptorSetLayout blendDescriptorSetLayout;
    std::vector<VkDescriptorSet> blendDescriptorSets;

    VkPipelineLayout geometryPipelineLayout;
    VkPipeline geometryPipeline;

    VkPipelineLayout lightingPipelineLayout;
    VkPipeline lightingPipeline;

    VkPipeline wireframePipeline;
    VkPipelineLayout wireframePipelineLayout;

    VkPipeline intrinsicOverlayPipeline;
    VkPipelineLayout intrinsicOverlayPipelineLayout;

    VkPipeline blendPipeline;
    VkPipelineLayout blendPipelineLayout;

};