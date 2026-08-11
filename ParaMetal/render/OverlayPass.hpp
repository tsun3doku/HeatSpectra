#pragma once

#include "framegraph/FramePass.hpp"
#include "framegraph/FrameGraphTypes.hpp"
#include "util/Units.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
class iODT;
class VulkanDevice;
class CommandPool;
class TimingRenderer;
class GridRenderer;
class GizmoRenderer;
class NavigationGizmoRenderer;
class ScreenTextRenderer;
class IntrinsicRenderer;
class OutlineRenderer;
class VkFrameGraphRuntime;

namespace render {
class ContactOverlayRenderer;
class HeatOverlayRenderer;
class PointOverlayRenderer;
class VoronoiOverlayRenderer;
class GeometryPass;
}

namespace render {
class OverlayPass : public Pass {
public:
    OverlayPass(
        VulkanDevice& device,
        MemoryAllocator& allocator,
        VkFrameGraphRuntime& frameGraphRuntime,
        ModelRegistry& resources,
        UniformBufferManager& ubo,
        GeometryPass& geometry,
        HeatOverlayRenderer& heatOverlayRenderer,
        VoronoiOverlayRenderer& voronoiOverlayRenderer,
        IntrinsicRenderer& intrinsicRenderer,
        GizmoRenderer& gizmoRenderer,
        ScreenTextRenderer& screenTextRenderer,
        NavigationGizmoRenderer& navigationGizmoRenderer,
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
    void record(const FrameContext& context, const SceneView& sceneView, const RenderFlags& flags, RenderServices& services) override;
    void destroy() override;

    IntrinsicRenderer* getIntrinsicRenderer() const;
    ContactOverlayRenderer* getContactOverlayRenderer() const;
    HeatOverlayRenderer* getHeatOverlayRenderer() const;
    PointOverlayRenderer* getPointOverlayRenderer() const;
    VoronoiOverlayRenderer* getVoronoiOverlayRenderer() const;
    void setTimingOverlayLines(const std::vector<std::string>& lines);
    void updateGrid(uint32_t frameIndex, const SceneView& sceneView, const glm::vec3& sceneExtent);
    void setWorldUnit(units::LengthUnit unit);

private:

    GeometryPass& geometryPass;
    HeatOverlayRenderer& heatOverlayRenderer;
    VoronoiOverlayRenderer& voronoiOverlayRenderer;
    IntrinsicRenderer& intrinsicRenderer;
    GizmoRenderer& gizmoRenderer;
    ScreenTextRenderer& screenTextRenderer;
    NavigationGizmoRenderer& navigationGizmoRenderer;
    ::VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    VkFrameGraphRuntime& frameGraphRuntime;
    ModelRegistry& modelRegistry;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    uint32_t maxFramesInFlight = 0;
    framegraph::PassId passId{};
    framegraph::ResourceId depthResolveId{};
    framegraph::ResourceId depthMsaaId{};

    std::unique_ptr<OutlineRenderer> outlineRenderer;
    std::unique_ptr<ContactOverlayRenderer> contactOverlayRenderer;
    std::unique_ptr<PointOverlayRenderer> pointOverlayRenderer;
    std::unique_ptr<TimingRenderer> timingOverlay;
    std::unique_ptr<GridRenderer> gridRenderer;
    bool ready = false;
};

} // namespace render
