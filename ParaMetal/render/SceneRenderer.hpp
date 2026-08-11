#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include "framegraph/FramePass.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "util/Units.hpp"

class UniformBufferManager;
class MemoryAllocator;
class ModelRegistry;
class FrameGraph;
class VulkanDevice;
class CommandPool;
class VkFrameGraphRuntime;
class IBLSystem;
class IntrinsicRenderer;
class GizmoRenderer;
class NavigationGizmoRenderer;
class ScreenTextRenderer;
namespace render { class ContactOverlayRenderer; }
namespace render { class HeatOverlayRenderer; }
namespace render { class PointOverlayRenderer; }
namespace render { class VoronoiOverlayRenderer; }

namespace render {
class GeometryPass;
class SurfacePass;
class LightingPass;
class OverlayPass;
class PickPass;
class BlendPass;

struct RenderFrameRequest {
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    VkExtent2D extent{};
    SceneView sceneView{};
    RenderFlags flags{};

};
}

struct GpuPassTiming {
    std::string name;
    float ms = 0.0f;
};

struct GpuTimingStats {
    float totalMs = 0.0f;
    std::vector<GpuPassTiming> passTimings;
};

inline constexpr std::array<float, 4> clearColorLinear = {0.0144f, 0.0144f, 0.0168f, 1.0f};

class SceneRenderer {
public:
    SceneRenderer(VulkanDevice& device, MemoryAllocator& allocator, FrameGraph& graph, VkFrameGraphRuntime& frameGraphRuntime, ModelRegistry& manager, UniformBufferManager& ubo, uint32_t framesInFlight, CommandPool& commandPool,
                  IBLSystem& iblSystem);
    ~SceneRenderer();

    void resize(VkExtent2D extent);
    void updateDescriptorSets();
    IntrinsicRenderer* getIntrinsicRenderer() const;
    render::ContactOverlayRenderer* getContactOverlayRenderer() const;
    render::HeatOverlayRenderer* getHeatOverlayRenderer() const;
    render::PointOverlayRenderer* getPointOverlayRenderer() const;
    render::VoronoiOverlayRenderer* getVoronoiOverlayRenderer() const;

    void setTimingOverlayLines(const std::vector<std::string>& lines);
    void updateGrid(uint32_t frameIndex, const render::SceneView& sceneView, const glm::vec3& sceneExtent);
    void setWorldUnit(units::LengthUnit unit);

    bool createCommandBuffers();
    void freeCommandBuffers();
    bool recordCommandBuffer(
        const render::RenderFrameRequest& frameRequest,
        render::RenderServices& services,
        bool insertComputeToGraphicsBarrier,
        VkPipelineStageFlags computeToGraphicsDstStageMask,
        const std::function<void(VkCommandBuffer)>& postRenderCommands = {});
    bool recordExternalCommandBuffer(
        VkCommandBuffer commandBuffer,
        const render::RenderFrameRequest& frameRequest,
        render::RenderServices& services,
        bool insertComputeToGraphicsBarrier,
        VkPipelineStageFlags computeToGraphicsDstStageMask);

    void cleanup();
    bool getGpuTimingStats(uint32_t frameIndex, GpuTimingStats& outStats) const;
    bool tryGetGpuFrameTimeMs(uint32_t frameIndex, float& outGpuMs) const;

    VkDescriptorSetLayout getGbufferDescriptorSetLayout() const;
    VkDescriptorSet getGeometryDescriptorSet(uint32_t frameIndex) const;

    const std::vector<VkCommandBuffer>& getCommandBuffers() const {
        return gbufferCommandBuffers;
    }

    bool isReady() const {
        return ready;
    }

private:
    bool initializePasses();
    void addPass(std::unique_ptr<render::Pass> pass);
    void rebuildOrderedPasses();
    void createPasses();
    void resizePasses(VkExtent2D extent);
    void updatePassDescriptors();
    void recordPasses(
        const render::FrameContext& frameContext,
        const render::SceneView& sceneView,
        const render::RenderFlags& flags,
        render::RenderServices& services,
        VkQueryPool passTimingQueryPool = VK_NULL_HANDLE,
        uint32_t passTimingQueryBase = 0);
    void destroyPasses();
    void createGpuTimingQueryPool();
    void destroyGpuTimingQueryPool();
    void recordComputeToGraphicsBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags dstStageMask);
    bool recordCommands(
        VkCommandBuffer commandBuffer,
        bool manageCommandBufferRecording,
        const render::RenderFrameRequest& frameRequest,
        render::RenderServices& services,
        bool insertComputeToGraphicsBarrier,
        VkPipelineStageFlags computeToGraphicsDstStageMask,
        const std::function<void(VkCommandBuffer)>& postRenderCommands);
    
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    FrameGraph& frameGraph;
    VkFrameGraphRuntime& frameGraphRuntime;
    ModelRegistry& modelRegistry;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    IBLSystem& iblSystem;

    render::GeometryPass* geometryPass = nullptr;
    render::SurfacePass* surfacePass = nullptr;
    render::LightingPass* lightingPass = nullptr;
    render::OverlayPass* overlayPass = nullptr;
    render::PickPass* pickPass = nullptr;
    render::BlendPass* blendPass = nullptr;
    std::unique_ptr<render::HeatOverlayRenderer> heatOverlayRenderer;
    std::unique_ptr<render::VoronoiOverlayRenderer> voronoiOverlayRenderer;
    std::unique_ptr<IntrinsicRenderer> intrinsicRenderer;
    std::unique_ptr<GizmoRenderer> gizmoRenderer;
    std::unique_ptr<NavigationGizmoRenderer> navigationGizmoRenderer;
    std::unique_ptr<ScreenTextRenderer> screenTextRenderer;

    uint32_t maxFramesInFlight = 0;
    VkQueryPool gpuTimingQueryPool = VK_NULL_HANDLE;
    float gpuTimestampPeriod = 0.0f;
    uint32_t gpuTimingQueriesPerFrame = 0;
    std::vector<std::string> gpuTimingPassNames;
    std::vector<uint8_t> gpuTimingValidFrames;
    std::vector<std::unique_ptr<render::Pass>> passes;
    std::vector<render::Pass*> orderedPasses;

    std::vector<VkCommandBuffer> gbufferCommandBuffers;

    bool ready = false;
};

