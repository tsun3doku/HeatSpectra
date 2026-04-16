#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <vector>

class ModelRegistry;
class UniformBufferManager;
class VulkanDevice;
class VkFrameGraphRuntime;

namespace render {

class GeometryPass : public Pass {
public:
    GeometryPass(
        VulkanDevice& device,
        VkFrameGraphRuntime& frameGraphRuntime,
        ModelRegistry& resources,
        UniformBufferManager& ubo,
        uint32_t framesInFlight,
        framegraph::PassId passId);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) override;
    void destroy() override;

    VkDescriptorSetLayout getDescriptorSetLayout() const;
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

private:
    bool createGeometryDescriptorPool(uint32_t maxFramesInFlight);
    bool createGeometryDescriptorSetLayout();
    bool createGeometryDescriptorSets(ModelRegistry& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    bool createGeometryPipeline();
    bool createStencilOnlyPipeline();

    ::VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};

    VkDescriptorPool geometryDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout geometryDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> geometryDescriptorSets;

    VkPipelineLayout geometryPipelineLayout = VK_NULL_HANDLE;
    VkPipeline geometryPipeline = VK_NULL_HANDLE;
    VkPipeline stencilOnlyPipeline = VK_NULL_HANDLE;
    bool ready = false;
};

} // namespace render


