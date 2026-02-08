#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <memory>

class Model;
class Grid;
class HeatSource;
class HeatSystem;
class HashGrid;
class UniformBufferManager;
class ResourceManager;
class DeferredRenderer;
class MemoryAllocator;
class VulkanDevice;
class CommandPool;
class WireframeRenderer;

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
    
    void createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight);
    void createSupportingHalfedgeDescriptorSetLayout();
    
    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight);
    void createIntrinsicNormalsDescriptorSetLayout();
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight);
    void createIntrinsicVertexNormalsDescriptorSetLayout();
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createGeometryPipeline(VkExtent2D extent);
    void createLightingPipeline(VkExtent2D swapchainExtent);
    void createSupportingHalfedgePipeline(VkExtent2D extent);
    void createIntrinsicNormalsPipeline(VkExtent2D extent);
    void createIntrinsicVertexNormalsPipeline(VkExtent2D extent);
    void createBlendPipeline(VkExtent2D extent);
    void createOutlinePipeline(VkExtent2D extent);
    void createStencilOnlyPipeline(VkExtent2D extent);

    void createCommandBuffers(uint32_t maxFramesInFlight);
    void freeCommandBuffers();
    void recordCommandBuffer(ResourceManager& resourceManager, HeatSystem& heatSystem, 
        class ModelSelection& modelSelection, class Gizmo& gizmo, WireframeRenderer& wireframeRenderer, std::vector<VkImageView> swapChainImageViews,
        uint32_t currentFrame, uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent, int wireframeMode, bool drawIntrinsicOverlay, bool drawHeatOverlay, bool drawIntrinsicNormals = false, bool drawIntrinsicVertexNormals = false, float normalLength = 0.05, bool drawHashGrid = false, bool drawSurfels = false, bool drawVoronoi = false, bool drawPoints = false, bool drawContactLines = false);

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
    
    VkDescriptorSetLayout getGbufferDescriptorSetLayout() const {
        return geometryDescriptorSetLayout;
    }

    const std::vector<VkCommandBuffer>& getCommandBuffers() const {
        return gbufferCommandBuffers;
    }

private:
    VulkanDevice& vulkanDevice;
    DeferredRenderer& deferredRenderer;
    ResourceManager& resourceManager;
    CommandPool& renderCommandPool;

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

    VkDescriptorPool outlineDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout outlineDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> outlineDescriptorSets;
    
    VkDescriptorPool supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelSupportingHalfedgeDescriptorSets;
    
    VkDescriptorPool intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicNormalsDescriptorSets;
    
    VkDescriptorPool intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    
    std::unordered_map<Model*, std::vector<VkDescriptorSet>> perModelIntrinsicVertexNormalsDescriptorSets;
    
    VkSampler depthSampler = VK_NULL_HANDLE;

    VkPipelineLayout geometryPipelineLayout;
    VkPipeline geometryPipeline;

    VkPipelineLayout lightingPipelineLayout;
    VkPipeline lightingPipeline;
    
    VkPipeline supportingHalfedgePipeline;
    VkPipelineLayout supportingHalfedgePipelineLayout;

    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;

    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;

    VkPipeline blendPipeline;
    VkPipelineLayout blendPipelineLayout;

    VkPipeline outlinePipeline = VK_NULL_HANDLE;
    VkPipelineLayout outlinePipelineLayout = VK_NULL_HANDLE;

    // Stencil-only pipeline for wire-only mode (writes depth/stencil but no color)
    VkPipeline stencilOnlyPipeline = VK_NULL_HANDLE;
};