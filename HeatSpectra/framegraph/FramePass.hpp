#pragma once

#include <vulkan/vulkan.h>
#include "scene/SceneView.hpp"

#include <vector>

class HeatSystem;
class VoronoiSystem;
class ModelSelection;
class GizmoController;
class WireframeRenderer;

namespace render {

struct FrameContext {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    uint32_t currentFrame = 0;
    VkExtent2D extent{};
};

struct RenderFlags {
    int wireframeMode = 0;
    bool drawIntrinsicOverlay = false;
    bool drawHeatOverlay = false;
    bool drawTimingOverlay = false;
    bool drawSurfels = false;
    bool drawVoronoi = false;
    bool drawPoints = false;
    bool drawContactLines = false;
};

struct OverlayParams {
    bool drawIntrinsicNormals = false;
    bool drawIntrinsicVertexNormals = false;
    float normalLength = 0.05f;
};

struct RenderServices {
    HeatSystem* heatSystem = nullptr;
    VoronoiSystem* voronoiSystem = nullptr;
    ModelSelection* modelSelection = nullptr;
    GizmoController* gizmoController = nullptr;
    WireframeRenderer* wireframeRenderer = nullptr;
};

class Pass {
public:
    virtual ~Pass() = default;

    virtual const char* name() const = 0;
    virtual void create() = 0;
    virtual void resize(VkExtent2D extent) = 0;
    virtual void updateDescriptors() = 0;
    virtual void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) = 0;
    virtual void destroy() = 0;
};

}
