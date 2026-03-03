#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <vector>

class UniformBufferManager;
class VulkanDevice;
class VkFrameGraphRuntime;

namespace render {

class LightingPass : public Pass {
public:
    LightingPass(
        VulkanDevice& device,
        VkFrameGraphRuntime& frameGraphRuntime,
        UniformBufferManager& ubo,
        uint32_t framesInFlight,
        framegraph::PassId passId,
        framegraph::ResourceId albedoResolveId,
        framegraph::ResourceId normalResolveId,
        framegraph::ResourceId positionResolveId);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) override;
    void destroy() override;

private:
    bool createLightingDescriptorPool(uint32_t maxFramesInFlight);
    bool createLightingDescriptorSetLayout();
    bool createLightingDescriptorSets(UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    bool createLightingPipeline();

    ::VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    UniformBufferManager& uniformBufferManager;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};
    framegraph::ResourceId albedoResolveId{};
    framegraph::ResourceId normalResolveId{};
    framegraph::ResourceId positionResolveId{};

    VkDescriptorPool lightingDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightingDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lightingDescriptorSets;

    VkPipelineLayout lightingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline lightingPipeline = VK_NULL_HANDLE;
    bool ready = false;
};

} // namespace render

