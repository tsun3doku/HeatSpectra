#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include "framegraph/FramePass.hpp"
#include "SceneColorSpace.hpp"

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

class Model;
class UniformBufferManager;
class MemoryAllocator;
class ResourceManager;
class FrameGraph;
class VulkanDevice;
class CommandPool;
class VkFrameGraphRuntime;

namespace render {
class GeometryPass;
class LightingPass;
class OverlayPass;
class BlendPass;

struct RenderFrameRequest {
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    VkExtent2D extent{};
    SceneView sceneView{};
    RenderFlags flags{};
    OverlayParams overlay{};
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

inline const auto& clearColorValues = clearColorLinear;

class SceneRenderer {
public:
    SceneRenderer(VulkanDevice& device, MemoryAllocator& allocator, FrameGraph& graph, VkFrameGraphRuntime& frameGraphRuntime, ResourceManager& manager, UniformBufferManager& ubo, uint32_t framesInFlight, CommandPool& commandPool);
    ~SceneRenderer();

    void resize(VkExtent2D extent);
    void updateDescriptorSets();
    void bindRemeshProduct(uint64_t socketKey, const RemeshProduct& product);
    void removeIntrinsicPackage(uint64_t packageKey);

    void setTimingOverlayLines(const std::vector<std::string>& lines);
    void updateGridLabels(const glm::vec3& gridSize);

    bool createCommandBuffers();
    void freeCommandBuffers();
    bool recordCommandBuffer(
        const render::RenderFrameRequest& frameRequest,
        render::RenderServices& services,
        bool insertComputeToGraphicsBarrier,
        VkPipelineStageFlags computeToGraphicsDstStageMask,
        const std::function<void(VkCommandBuffer)>& postRenderCommands = {});

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
        const render::OverlayParams& overlayParams,
        render::RenderServices& services,
        VkQueryPool passTimingQueryPool = VK_NULL_HANDLE,
        uint32_t passTimingQueryBase = 0);
    void destroyPasses();
    void createGpuTimingQueryPool();
    void destroyGpuTimingQueryPool();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    FrameGraph& frameGraph;
    VkFrameGraphRuntime& frameGraphRuntime;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    render::GeometryPass* geometryPass = nullptr;
    render::LightingPass* lightingPass = nullptr;
    render::OverlayPass* overlayPass = nullptr;
    render::BlendPass* blendPass = nullptr;

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
