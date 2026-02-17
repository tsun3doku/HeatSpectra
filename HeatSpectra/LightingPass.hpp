#pragma once

#include "FrameGraph.hpp"

#include <vector>

class SceneRenderer;
class UniformBufferManager;
class VulkanDevice;
class FrameGraph;

namespace render {

class LightingPass : public Pass {
public:
    explicit LightingPass(SceneRenderer& sceneRenderer);

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
    void createLightingDescriptorPool(uint32_t maxFramesInFlight);
    void createLightingDescriptorSetLayout();
    void createLightingDescriptorSets(UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void createLightingPipeline(VkExtent2D swapchainExtent);

    SceneRenderer& sceneRenderer;
    ::VulkanDevice& vulkanDevice;
    ::FrameGraph& frameGraph;

    VkDescriptorPool lightingDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightingDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lightingDescriptorSets;

    VkPipelineLayout lightingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline lightingPipeline = VK_NULL_HANDLE;
};

} // namespace render

