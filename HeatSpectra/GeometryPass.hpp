#pragma once

#include "FrameGraph.hpp"

#include <vector>

class SceneRenderer;
class ResourceManager;
class UniformBufferManager;
class VulkanDevice;
class FrameGraph;

namespace render {

class GeometryPass : public Pass {
public:
    explicit GeometryPass(SceneRenderer& sceneRenderer);

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

    VkDescriptorSetLayout getDescriptorSetLayout() const;
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

private:
    void createGeometryDescriptorPool(uint32_t maxFramesInFlight);
    void createGeometryDescriptorSetLayout();
    void createGeometryDescriptorSets(ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void createGeometryPipeline(VkExtent2D extent);
    void createStencilOnlyPipeline(VkExtent2D extent);

    SceneRenderer& sceneRenderer;
    ::VulkanDevice& vulkanDevice;
    ::FrameGraph& frameGraph;

    VkDescriptorPool geometryDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout geometryDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> geometryDescriptorSets;

    VkPipelineLayout geometryPipelineLayout = VK_NULL_HANDLE;
    VkPipeline geometryPipeline = VK_NULL_HANDLE;
    VkPipeline stencilOnlyPipeline = VK_NULL_HANDLE;
};

} // namespace render

