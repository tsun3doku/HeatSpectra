#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

class ResourceManager;
class HeatSystem;
class ModelSelection;
class Gizmo;
class WireframeRenderer;

namespace render {

struct FrameContext {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    uint32_t currentFrame = 0;
    uint32_t imageIndex = 0;
    uint32_t maxFramesInFlight = 0;
    VkExtent2D extent{};
    const std::vector<VkImageView>* swapChainImageViews = nullptr;
};

struct SceneView {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float _padding0 = 0.0f;
};

struct RenderFlags {
    int wireframeMode = 0;
    bool drawIntrinsicOverlay = false;
    bool drawHeatOverlay = false;
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
    ResourceManager* resourceManager = nullptr;
    HeatSystem* heatSystem = nullptr;
    ModelSelection* modelSelection = nullptr;
    Gizmo* gizmo = nullptr;
    WireframeRenderer* wireframeRenderer = nullptr;
};

class Pass {
public:
    virtual ~Pass() = default;

    virtual const char* name() const = 0;
    virtual void create() = 0;
    virtual void resize(VkExtent2D extent) = 0;
    virtual void updateDescriptors() = 0;
    virtual void record(
        const FrameContext& frameContext,
        const SceneView& sceneView,
        const RenderFlags& flags,
        const OverlayParams& overlayParams,
        RenderServices& services) = 0;
    virtual void destroy() = 0;
};

} // namespace render
