#pragma once

#include "FrameGraph.hpp"

#include <vector>

class SceneRenderer;
class VulkanDevice;
class FrameGraph;

namespace render {

class BlendPass : public Pass {
public:
    explicit BlendPass(SceneRenderer& sceneRenderer);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(
        const FrameContext& frameContext,
        const SceneView& sceneView,
        const RenderFlags& flags,
        const OverlayParams& overlayParams,
        RenderServices& services) override;
    void destroy() override;

private:
    void createBlendDescriptorPool(uint32_t maxFramesInFlight);
    void createBlendDescriptorSetLayout();
    void createBlendDescriptorSets(uint32_t maxFramesInFlight);
    void createBlendPipeline(VkExtent2D extent);

    SceneRenderer& sceneRenderer;
    ::VulkanDevice& vulkanDevice;
    ::FrameGraph& frameGraph;

    VkDescriptorPool blendDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout blendDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> blendDescriptorSets;

    VkPipelineLayout blendPipelineLayout = VK_NULL_HANDLE;
    VkPipeline blendPipeline = VK_NULL_HANDLE;
};

} // namespace render

