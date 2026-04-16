#pragma once

#include <vulkan/vulkan.h>
#include "scene/SceneView.hpp"

#include <vector>

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
    bool drawTimingOverlay = false;
};

struct RenderServices {
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
    virtual void record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) = 0;
    virtual void destroy() = 0;
};

}
