#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class Model;
class ResourceManager;
class UniformBufferManager;
class iODT;
class VulkanDevice;
class CommandPool;
class TimingRenderer;
class GridRenderer;
class GizmoRenderer;
class IntrinsicRenderer;
class OutlineRenderer;
class VkFrameGraphRuntime;

namespace render {
class GeometryPass;
}

namespace render {
class OverlayPass : public Pass {
public:
    OverlayPass(
        VulkanDevice& device,
        VkFrameGraphRuntime& frameGraphRuntime,
        ResourceManager& resources,
        UniformBufferManager& ubo,
        GeometryPass& geometry,
        uint32_t framesInFlight,
        CommandPool& pool,
        framegraph::PassId passId,
        framegraph::ResourceId depthResolveId,
        framegraph::ResourceId depthMsaaId);
    ~OverlayPass() override;

    const char* name() const override;
    void create() override;
    void resize(VkExtent2D extent) override;
    void updateDescriptors() override;
    void record(const FrameContext& context, const SceneView& sceneView, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) override;
    void destroy() override;

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight);
    void setTimingOverlayLines(const std::vector<std::string>& lines);
    void updateGridLabels(const glm::vec3& gridSize);

private:

    GeometryPass& geometryPass;
    ::VulkanDevice& vulkanDevice;
    VkFrameGraphRuntime& frameGraphRuntime;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};
    framegraph::ResourceId depthResolveId{};
    framegraph::ResourceId depthMsaaId{};

    std::unique_ptr<OutlineRenderer> outlineRenderer;
    std::unique_ptr<IntrinsicRenderer> intrinsicRenderer;
    std::unique_ptr<TimingRenderer> timingOverlay;
    std::unique_ptr<GridRenderer> gridRenderer;
    std::unique_ptr<GizmoRenderer> gizmoRenderer;
    bool ready = false;
};

} // namespace render

