#pragma once

#include "runtime/RuntimeProducts.hpp"

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class CommandPool;
class iODT;
class ModelRegistry;

class IntrinsicRenderer {
public:
    IntrinsicRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& commandPool,
        VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    ~IntrinsicRenderer();
    void cleanup();

    void bindRemeshProduct(uint64_t socketKey, const RemeshProduct& product);
    void removeIntrinsicPackage(uint64_t packageKey);
    void renderSupportingHalfedges(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ModelRegistry& resourceManager);
    void renderIntrinsicNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ModelRegistry& resourceManager, float normalLength);
    void renderIntrinsicVertexNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame, const ModelRegistry& resourceManager, float normalLength);

private:
    bool initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();
    void pruneStalePackageResources(const ModelRegistry& resourceManager);
    void releaseDescriptorSetsForPackage(uint64_t packageKey);
    void allocateDescriptorSetsForPackage(uint64_t packageKey, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForPackage(uint64_t packageKey, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForPackage(uint64_t packageKey, uint32_t maxFramesInFlight);
    void updatePayloadDescriptorSetsForPackage(uint64_t packageKey, const RemeshProduct& product);
    void updatePayloadNormalsDescriptorSetsForPackage(uint64_t packageKey, const RemeshProduct& product);
    void updatePayloadVertexNormalsDescriptorSetsForPackage(uint64_t packageKey, const RemeshProduct& product);

    bool createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight);
    bool createSupportingHalfedgeDescriptorSetLayout();

    bool createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicNormalsDescriptorSetLayout();
    bool createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight);
    bool createIntrinsicVertexNormalsDescriptorSetLayout();

    bool createSupportingHalfedgePipeline(VkRenderPass renderPass, uint32_t subpassIndex);
    bool createIntrinsicNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex);
    bool createIntrinsicVertexNormalsPipeline(VkRenderPass renderPass, uint32_t subpassIndex);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& allocator;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    uint32_t maxFramesInFlight = 0;

    VkDescriptorPool supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> supportingHalfedgeDescriptorSetsByPackage;

    VkImage wireframeTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory wireframeTextureMemory = VK_NULL_HANDLE;
    VkImageView wireframeTextureView = VK_NULL_HANDLE;
    VkSampler wireframeTextureSampler = VK_NULL_HANDLE;

    VkDescriptorPool intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> intrinsicNormalsDescriptorSetsByPackage;

    VkDescriptorPool intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> intrinsicVertexNormalsDescriptorSetsByPackage;
    std::unordered_map<uint64_t, RemeshProduct> remeshProductsByPackageKey;
    std::unordered_map<uint64_t, uint32_t> runtimeModelIdByPackageKey;

    VkPipeline supportingHalfedgePipeline = VK_NULL_HANDLE;
    VkPipelineLayout supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;

    bool initialized = false;
};

