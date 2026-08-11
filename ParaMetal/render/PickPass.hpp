#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

class ModelRegistry;
class UniformBufferManager;
class VulkanDevice;
class VkFrameGraphRuntime;
class GizmoRenderer;

namespace render {

class GeometryPass;

class PickPass : public Pass {
public:
    PickPass(
        VulkanDevice& device,
        VkFrameGraphRuntime& frameGraphRuntime,
        ModelRegistry& resources,
        GeometryPass& geometry,
        GizmoRenderer& gizmoRenderer,
        framegraph::PassId passId);

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) override;
    void destroy() override;

private:
    struct PickPushConstant {
        alignas(16) glm::mat4 modelMatrix;
        uint32_t pickId = 0;
        uint32_t padding[3] = {};
    };

    bool createModelPipeline();

    VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    ModelRegistry& modelRegistry;
    GeometryPass& geometryPass;
    GizmoRenderer& gizmoRenderer;
    framegraph::PassId passId{};

    VkPipelineLayout modelPipelineLayout = VK_NULL_HANDLE;
    VkPipeline modelPipeline = VK_NULL_HANDLE;
    bool ready = false;
};

}
