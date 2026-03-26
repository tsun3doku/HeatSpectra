#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "FrameGraphTypes.hpp"

class VulkanDevice;
class MemoryAllocator;

class VkFrameGraphRuntime {
public:
    VkFrameGraphRuntime() = default;

    bool rebuild(
        const VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const framegraph::FrameGraphResult& frameGraphResult,
        const std::vector<VkImageView>& swapChainImageViews,
        VkExtent2D extent,
        uint32_t maxFramesInFlight);
    void cleanup(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);

    const framegraph::FrameGraphResult& getFrameGraphResult() const {
        return frameGraphResult;
    }

    VkRenderPass getRenderPass() const {
        return renderPass;
    }

    VkFramebuffer getFramebuffer(uint32_t currentFrame, uint32_t imageIndex) const;
    VkPipelineStageFlags getComputeToGraphicsWaitDstStageMask() const {
        return computeToGraphicsWaitDstStageMask;
    }

    const std::vector<VkImage>& getResourceImages(framegraph::ResourceId resourceId) const;
    const std::vector<VkImageView>& getResourceViews(framegraph::ResourceId resourceId) const;
    const std::vector<VkImageView>& getResourceDepthSamplerViews(framegraph::ResourceId resourceId) const;
    const std::vector<VkImageView>& getResourceStencilSamplerViews(framegraph::ResourceId resourceId) const;

private:
    struct SyncState {
        VkPipelineStageFlags stageMask = 0;
        VkAccessFlags accessMask = 0;
        bool isWrite = false;
    };

    struct ResourceStorage {
        std::vector<VkImage> images;
        std::vector<VkDeviceMemory> imageMemories;
        std::vector<VkImageView> views;
        std::vector<VkImageView> depthSamplerViews;
        std::vector<VkImageView> stencilSamplerViews;
        bool createDepthSamplerViews = false;
        bool createStencilSamplerViews = false;
    };

    struct AttachmentRefBuildContext {
        const std::vector<uint32_t>* attachmentIndexByResource = nullptr;
        framegraph::ResourceLayout defaultLayout = framegraph::ResourceLayout::Undefined;
        framegraph::ImageAspect defaultAspect = framegraph::ImageAspect::None;
    };

    void loadResult(const framegraph::FrameGraphResult& result);
    bool createRenderPass(const VulkanDevice& vulkanDevice);
    bool createImageViews(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);
    bool createFramebuffers(const std::vector<VkImageView>& swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight, const VulkanDevice& vulkanDevice);
    void cleanupFramebuffers(const VulkanDevice& vulkanDevice);
    void cleanupImages(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight);

    SyncState usageToSync(const framegraph::ResourceUse& use) const;
    static bool hasDepthOrStencilAspect(framegraph::ImageAspect aspectMask);
    static void destroyImageViewAt(VkDevice device, std::vector<VkImageView>& views, uint32_t frameIndex);
    static void destroyImageAt(VkDevice device, std::vector<VkImage>& images, uint32_t frameIndex);
    bool buildVkAttachmentRef(const framegraph::AttachmentReference& attachmentRef, const AttachmentRefBuildContext& buildContext, VkAttachmentReference2& outRef) const;
    void addOrMergeSubpassDependency(
        std::map<uint64_t, VkSubpassDependency2>& dependencyMap,
        uint32_t srcPass,
        uint32_t dstPass,
        const SyncState& src,
        const SyncState& dst) const;

    const ResourceStorage* findResourceStorage(framegraph::ResourceId resourceId) const;

    static const std::vector<VkImage>& emptyImages();
    static const std::vector<VkImageView>& emptyViews();

    framegraph::FrameGraphResult frameGraphResult{};
    std::vector<ResourceStorage> resourceStorages;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineStageFlags computeToGraphicsWaitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    uint32_t swapchainImageCount = 0;
    bool isLoaded = false;
};
