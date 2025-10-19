#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>

class Model;
class Grid;
class HeatSource;
class HeatSystem;
class UniformBufferManager;
class ResourceManager;
class DeferredRenderer;
class MemoryAllocator;
class VulkanDevice;
class CommandPool;

const std::vector<float> clearColorValues = { {0.013f, 0.0138f, 0.0135f, 1.0f} };

class GBuffer {
public:
    GBuffer(VulkanDevice& vulkanDevice, DeferredRenderer& deferredRenderer, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager,
        uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight, CommandPool& renderCommandPool, bool drawWireframe);
    ~GBuffer();

    void createFramebuffers(std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight);
    void updateDescriptorSets(uint32_t maxFramesInFlight);

    void createGeometryDescriptorPool(uint32_t maxFramesInFlight);
    void createGeometryDescriptorSetLayout();
    void createGeometryDescriptorSets(ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createLightingDescriptorPool(uint32_t maxFramesInFlight);
    void createLightingDescriptorSetLayout();
    void createLightingDescriptorSets(UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createBlendDescriptorPool(uint32_t maxFramesInFlight);
    void createBlendDescriptorSetLayout();
    void createBlendDescriptorSets(uint32_t maxFramesInFlight);

    void createOutlineDescriptorPool(uint32_t maxFramesInFlight);
    void createOutlineDescriptorSetLayout();
    void createOutlineDescriptorSets(uint32_t maxFramesInFlight);
    void createDepthSampler();

    void createGeometryPipeline(VkExtent2D extent);
    void createWireframePipeline(VkExtent2D extent);
    void createLightingPipeline(VkExtent2D swapchainExtent);
    void createIntrinsicOverlayPipeline(VkExtent2D extent);
    void createBlendPipeline(VkExtent2D extent);
    void createOutlinePipeline(VkExtent2D extent);

    void createCommandBuffers(uint32_t maxFramesInFlight);
    void freeCommandBuffers();
    void recordCommandBuffer(ResourceManager& resourceManager, HeatSystem& heatSystem, 
        class ModelSelection& modelSelection, class Gizmo& gizmo, std::vector<VkImageView> swapChainImageViews,
        uint32_t currentFrame, uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent, bool drawWireframe, bool drawCommonSubdivision);

    void cleanupFramebuffers(uint32_t maxFramesInFlight);
    void cleanup(uint32_t maxFramesInFlight);

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
    DeferredRenderer& deferredRenderer;
    CommandPool& renderCommandPool;  // For command buffer allocation

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

    VkDescriptorPool outlineDescriptorPool;
    VkDescriptorSetLayout outlineDescriptorSetLayout;
    std::vector<VkDescriptorSet> outlineDescriptorSets;
    VkSampler depthSampler;

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

    VkPipeline outlinePipeline = VK_NULL_HANDLE;
    VkPipelineLayout outlinePipelineLayout = VK_NULL_HANDLE;
};