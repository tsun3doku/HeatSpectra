#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <vector>

class VulkanDevice;
class VkFrameGraphRuntime;
class CommandPool;

namespace render {

class BlendPass : public Pass {
public:
    BlendPass(
        VulkanDevice& device,
        CommandPool& commandPool,
        VkFrameGraphRuntime& frameGraphRuntime,
        uint32_t framesInFlight,
        framegraph::PassId passId,
        framegraph::ResourceId surfaceResolveId,
        framegraph::ResourceId lineResolveId,
        framegraph::ResourceId lightingResolveId,
        framegraph::ResourceId albedoResolveId);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) override;
    void destroy() override;

private:
    bool createBlendDescriptorPool(uint32_t maxFramesInFlight);
    bool createBlendDescriptorSetLayout();
    bool createBlendDescriptorSets(uint32_t maxFramesInFlight);
    bool createBlendPipeline();
    bool createBackgroundResources();
    void destroyBackgroundResources();

    ::VulkanDevice& vulkanDevice;
    CommandPool& commandPool;
    VkFrameGraphRuntime& frameGraphRuntime;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};
    framegraph::ResourceId surfaceResolveId{};
    framegraph::ResourceId lineResolveId{};
    framegraph::ResourceId lightingResolveId{};
    framegraph::ResourceId albedoResolveId{};

    VkDescriptorPool blendDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout blendDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> blendDescriptorSets;

    VkPipelineLayout blendPipelineLayout = VK_NULL_HANDLE;
    VkPipeline blendPipeline = VK_NULL_HANDLE;

    VkImage backgroundImage = VK_NULL_HANDLE;
    VkDeviceMemory backgroundImageMemory = VK_NULL_HANDLE;
    VkImageView backgroundImageView = VK_NULL_HANDLE;
    VkSampler backgroundSampler = VK_NULL_HANDLE;
    bool ready = false;
};

} // namespace render

