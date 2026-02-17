#pragma once

#include <vulkan/vulkan.h>
#include "FramePass.hpp"
#include "FrameGraphVk.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanDevice;

class FrameGraph {
public:
    FrameGraph(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight);
    ~FrameGraph();

    void clearGraphDesc();
    uint32_t addResourceDesc(fg::ResourceDesc desc);
    void addPassDesc(fg::PassDesc passDesc);
    void createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat);
    void createImageViews(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D extent, uint32_t maxFramesInFlight);
    void createFramebuffers(const std::vector<VkImageView>& swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight);
    void cleanupFramebuffers();
    void cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void cleanup(VulkanDevice& vulkanDevice);
    VkFramebuffer getFramebuffer(uint32_t currentFrame, uint32_t imageIndex) const;
    void addPass(std::unique_ptr<render::Pass> pass);
    void createPasses();
    void resizePasses(VkExtent2D extent);
    void updatePassDescriptors();
    void recordPasses(
        const render::FrameContext& frameContext,
        const render::SceneView& sceneView,
        const render::RenderFlags& flags,
        const render::OverlayParams& overlayParams,
        render::RenderServices& services,
        VkQueryPool passTimingQueryPool = VK_NULL_HANDLE,
        uint32_t passTimingQueryBase = 0);
    void destroyPasses();
    uint32_t getSubpassIndex(const char* passName) const;
    void setComputeSyncEnabled(bool enabled);
    VkPipelineStageFlags getComputeWaitDstStageMask() const;
    void recordComputeToGraphicsBarrier(VkCommandBuffer commandBuffer) const;
    const std::vector<VkBuffer>& getResourceBuffers(const char* resourceName) const;
    const std::vector<VkImage>& getResourceImages(const char* resourceName) const;
    const std::vector<VkImageView>& getResourceViews(const char* resourceName) const;
    const std::vector<VkImageView>& getResourceDepthSamplerViews(const char* resourceName) const;
    const std::vector<VkImageView>& getResourceStencilSamplerViews(const char* resourceName) const;
    std::vector<std::string> getPassNames() const;

    const VkRenderPass& getRenderPass() const {
        return renderPass;
    }

    const std::vector<VkImage>& getAlbedoImages() const;
    const std::vector<VkImage>& getNormalImages() const;
    const std::vector<VkImage>& getPositionImages() const;
    const std::vector<VkImage>& getDepthImages() const;
    const std::vector<VkImage>& getLineOverlayImages() const;
    const std::vector<VkImage>& getSurfaceOverlayImages() const;

    const std::vector<VkImageView>& getAlbedoViews() const;
    const std::vector<VkImageView>& getNormalViews() const;
    const std::vector<VkImageView>& getPositionViews() const;
    const std::vector<VkImageView>& getDepthViews() const;
    const std::vector<VkImageView>& getLineOverlayViews() const;
    const std::vector<VkImageView>& getSurfaceOverlayViews() const;

    const std::vector<VkImageView>& getAlbedoResolveViews() const;
    const std::vector<VkImageView>& getNormalResolveViews() const;
    const std::vector<VkImageView>& getPositionResolveViews() const;
    const std::vector<VkImageView>& getDepthResolveViews() const;
    const std::vector<VkImageView>& getDepthResolveSamplerViews() const;
    const std::vector<VkImageView>& getStencilMSAASamplerViews() const;
    const std::vector<VkImageView>& getLineOverlayResolveViews() const;
    const std::vector<VkImageView>& getSurfaceOverlayResolveViews() const;
    const std::vector<VkImageView>& getLightingViews() const;
    const std::vector<VkImageView>& getLightingResolveViews() const;
    const std::vector<VkImage>& getDepthResolveImages() const;

    VkDeviceSize getTransientNoAliasBytes() const {
        return transientNoAliasBytes;
    }

    VkDeviceSize getTransientAliasedBytes() const {
        return transientAliasedBytes;
    }

private:
    struct ResourceStorage {
        std::vector<VkBuffer> buffers;
        std::vector<VkDeviceMemory> bufferMemories;
        std::vector<VkImage> images;
        std::vector<VkDeviceMemory> imageMemories;
        std::vector<VkImageView> views;
        std::vector<VkImageView> depthSamplerViews;
        std::vector<VkImageView> stencilSamplerViews;
        bool createDepthSamplerViews = false;
        bool createStencilSamplerViews = false;
    };

    struct ResourceLifetimeRange {
        int32_t firstPass = -1;
        int32_t lastPass = -1;

        bool isValid() const {
            return firstPass >= 0 && lastPass >= firstPass;
        }
    };

    void buildGraph(VkFormat swapchainImageFormat, VkExtent2D extent);
    void cullUnusedGraph();
    void compilePassDag();
    void compileTransientAliasingPlan();
    bool canAliasResources(uint32_t resourceA, uint32_t resourceB) const;
    bool hasLifetimeOverlap(uint32_t resourceA, uint32_t resourceB) const;
    std::vector<fg::ResourceUse> deriveUses(const fg::PassDesc& passDesc) const;
    void rebuildOrderedPasses();
    const ResourceStorage* findResourceStorage(const char* resourceName) const;
    static const std::vector<VkBuffer>& emptyBuffers();
    static const std::vector<VkImage>& emptyImages();
    static const std::vector<VkImageView>& emptyViews();

    VulkanDevice& vulkanDevice;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t swapchainImageCount = 0;

    bool graphBuilt = false;
    std::vector<fg::ResourceDesc> registeredResourceDecls;
    std::vector<fg::PassDesc> registeredPassDecls;
    std::vector<fg::ResourceDesc> resourceDecls;
    std::vector<fg::PassDesc> passDecls;
    std::unordered_map<std::string, uint32_t> resourceIdByName;
    std::unordered_map<std::string, uint32_t> subpassIndexByName;
    std::vector<uint32_t> attachmentResourceOrder;
    std::vector<ResourceLifetimeRange> resourceLifetimes;
    std::vector<int32_t> aliasGroupByResource;
    std::vector<std::vector<uint32_t>> aliasGroups;
    VkDeviceSize transientNoAliasBytes = 0;
    VkDeviceSize transientAliasedBytes = 0;
    std::vector<ResourceStorage> resourceStorages;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<std::unique_ptr<render::Pass>> passes;
    std::vector<render::Pass*> orderedPasses;
    bool computeSyncEnabled = false;
};
