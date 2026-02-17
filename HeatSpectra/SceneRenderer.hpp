#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

class Model;
class HeatSystem;
class UniformBufferManager;
class ResourceManager;
class FrameGraph;
class VulkanDevice;
class CommandPool;
class WireframeRenderer;

namespace render {
class GeometryPass;
class LightingPass;
class OverlayPass;
class BlendPass;
}

struct GpuPassTiming {
    std::string name;
    float ms = 0.0f;
};

struct GpuTimingStats {
    float totalMs = 0.0f;
    std::vector<GpuPassTiming> passTimings;
};

const std::vector<float> clearColorValues = { {0.013f, 0.0138f, 0.0135f, 1.0f} };

class SceneRenderer {
public:
    SceneRenderer(
        VulkanDevice& vulkanDevice,
        FrameGraph& frameGraph,
        ResourceManager& resourceManager,
        UniformBufferManager& uniformBufferManager,
        uint32_t width,
        uint32_t height,
        VkExtent2D swapchainExtent,
        const std::vector<VkImageView> swapChainImageViews,
        VkFormat swapchainImageFormat,
        uint32_t maxFramesInFlight,
        CommandPool& renderCommandPool,
        bool drawWireframe);
    ~SceneRenderer();

    void resize(VkExtent2D extent);
    void updateDescriptorSets(uint32_t maxFramesInFlight);

    void allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateNormalsDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    void allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight);
    void updateVertexNormalsDescriptorSetsForModel(Model* model, class iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);

    void createCommandBuffers(uint32_t maxFramesInFlight);
    void freeCommandBuffers();
    void recordCommandBuffer(
        ResourceManager& resourceManager,
        HeatSystem& heatSystem,
        class ModelSelection& modelSelection,
        class Gizmo& gizmo,
        WireframeRenderer& wireframeRenderer,
        std::vector<VkImageView> swapChainImageViews,
        uint32_t currentFrame,
        uint32_t imageIndex,
        uint32_t maxFramesInFlight,
        VkExtent2D extent,
        int wireframeMode,
        bool drawIntrinsicOverlay,
        bool drawHeatOverlay,
        bool drawIntrinsicNormals = false,
        bool drawIntrinsicVertexNormals = false,
        float normalLength = 0.05f,
        bool drawSurfels = false,
        bool drawVoronoi = false,
        bool drawPoints = false,
        bool drawContactLines = false);

    void cleanup(uint32_t maxFramesInFlight);
    bool tryGetGpuTimingStats(uint32_t frameIndex, GpuTimingStats& outStats) const;
    bool tryGetGpuFrameTimeMs(uint32_t frameIndex, float& outGpuMs) const;

    VkDescriptorSetLayout getGbufferDescriptorSetLayout() const;
    VkDescriptorSet getGeometryDescriptorSet(uint32_t frameIndex) const;

    const std::vector<VkCommandBuffer>& getCommandBuffers() const {
        return gbufferCommandBuffers;
    }

    VulkanDevice& getVulkanDevice() {
        return vulkanDevice;
    }

    FrameGraph& getFrameGraph() {
        return frameGraph;
    }

    ResourceManager& getResourceManager() {
        return resourceManager;
    }

    UniformBufferManager& getUniformBufferManager() {
        return uniformBufferManager;
    }

    uint32_t getMaxFramesInFlight() const {
        return maxFramesInFlight;
    }

    VkExtent2D getRenderExtent() const {
        return renderExtent;
    }

private:
    void initializePasses();
    void createGpuTimingQueryPool();
    void destroyGpuTimingQueryPool();

    VulkanDevice& vulkanDevice;
    FrameGraph& frameGraph;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    render::GeometryPass* geometryPass = nullptr;
    render::LightingPass* lightingPass = nullptr;
    render::OverlayPass* overlayPass = nullptr;
    render::BlendPass* blendPass = nullptr;

    uint32_t maxFramesInFlight = 0;
    VkExtent2D renderExtent{};
    VkQueryPool gpuTimingQueryPool = VK_NULL_HANDLE;
    float gpuTimestampPeriod = 0.0f;
    uint32_t gpuTimingQueriesPerFrame = 0;
    std::vector<std::string> gpuTimingPassNames;
    std::vector<uint8_t> gpuTimingValidFrames;

    std::vector<VkCommandBuffer> gbufferCommandBuffers;
};
