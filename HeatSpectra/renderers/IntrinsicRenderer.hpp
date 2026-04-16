#pragma once

#include "runtime/RemeshDisplayController.hpp"

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class CommandPool;
class iODT;

class IntrinsicRenderer {
public:
    IntrinsicRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& commandPool,
        VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    ~IntrinsicRenderer();
    void cleanup();

    void apply(uint64_t socketKey, const RemeshDisplayController::Config& config);
    void remove(uint64_t socketKey);
    void renderSupportingHalfedges(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void renderIntrinsicNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void renderIntrinsicVertexNormals(VkCommandBuffer commandBuffer, uint32_t currentFrame);

private:
    bool initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight, uint32_t subpassIndex);
    uint32_t calculateMipLevels(uint32_t width, uint32_t height);
    bool createWireframeTexture();
    void pruneStaleSocketResources();
    void releaseDescriptorSetsForSocket(uint64_t socketKey);
    void allocateDescriptorSetsForSocket(uint64_t socketKey, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForSocket(uint64_t socketKey, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForSocket(uint64_t socketKey, uint32_t maxFramesInFlight);
    void updatePayloadDescriptorSetsForSocket(uint64_t socketKey, const RemeshDisplayController::Config& config);
    void updatePayloadNormalsDescriptorSetsForSocket(uint64_t socketKey, const RemeshDisplayController::Config& config);
    void updatePayloadVertexNormalsDescriptorSetsForSocket(uint64_t socketKey, const RemeshDisplayController::Config& config);

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
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> supportingHalfedgeDescriptorSetsBySocket;

    VkImage wireframeTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory wireframeTextureMemory = VK_NULL_HANDLE;
    VkImageView wireframeTextureView = VK_NULL_HANDLE;
    VkSampler wireframeTextureSampler = VK_NULL_HANDLE;

    VkDescriptorPool intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> intrinsicNormalsDescriptorSetsBySocket;

    VkDescriptorPool intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, std::vector<VkDescriptorSet>> intrinsicVertexNormalsDescriptorSetsBySocket;
    std::unordered_map<uint64_t, RemeshDisplayController::Config> remeshConfigsBySocketKey;

    VkPipeline supportingHalfedgePipeline = VK_NULL_HANDLE;
    VkPipelineLayout supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;

    bool initialized = false;
};
