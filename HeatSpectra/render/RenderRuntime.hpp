#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include "util/ComputeTiming.hpp"
#include "framegraph/FrameStats.hpp"
#include "framegraph/FramePass.hpp"
#include "WindowRuntimeState.hpp"

class VulkanDevice;
class SwapchainManager;
class CommandPool;
class FrameSync;
class CameraController;
class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class MeshModifiers;
class InputController;
class LightingSystem;
class MaterialSystem;
class FrameGraph;
class SceneRenderer;
class ModelSelection;
class GizmoController;
class WireframeRenderer;
class FrameController;
class VkFrameGraphBackend;
class VkFrameGraphRuntime;
class HeatSystem;
class VoronoiSystem;

struct RenderRuntimeServices {
    ResourceManager& resourceManager;
    MeshModifiers& meshModifiers;
    UniformBufferManager& uniformBufferManager;
    HeatSystem* heatSystem = nullptr;
    VoronoiSystem* voronoiSystem = nullptr;
    InputController& inputController;
    LightingSystem& lightingSystem;
    MaterialSystem& materialSystem;
};

class RenderRuntime {
public:
    RenderRuntime(
        const WindowRuntimeState& windowState,
        VulkanDevice& vulkanDevice,
        SwapchainManager& swapchainManager,
        CommandPool& renderCommandPool,
        FrameSync& frameSync,
        CameraController& cameraController,
        std::atomic<bool>& isOperating,
        std::atomic<bool>& isShuttingDown);
    ~RenderRuntime();

    bool initializeBase(VkFormat swapChainFormat, VkExtent2D extent, MemoryAllocator& allocator, ResourceManager& resourceManager, UniformBufferManager& ubo);
    bool initializeFrameController(const RenderRuntimeServices& services);

    bool initializeSyncObjects();
    void shutdownSyncObjects();
    void renderFrame(const render::RenderFlags& flags, const render::OverlayParams& overlay, bool allowHeatSolve);
    void cleanupSwapChain();
    void cleanup();

    FrameGraph& getFrameGraph();
    const FrameGraph& getFrameGraph() const;
    VkFrameGraphRuntime& getFrameGraphRuntime();
    const VkFrameGraphRuntime& getFrameGraphRuntime() const;
    SceneRenderer& getSceneRenderer();
    const SceneRenderer& getSceneRenderer() const;
    ModelSelection& getModelSelection();
    const ModelSelection& getModelSelection() const;
    GizmoController& getGizmoController();
    const GizmoController& getGizmoController() const;

private:
    const WindowRuntimeState& windowState;
    VulkanDevice& vulkanDevice;
    SwapchainManager& swapchainManager;
    CommandPool& renderCommandPool;
    FrameSync& frameSync;
    CameraController& cameraController;
    std::atomic<bool>& isOperating;
    std::atomic<bool>& isShuttingDown;

    FrameStats frameStats;
    ComputeTiming computeTiming;

    std::unique_ptr<FrameGraph> frameGraph;
    std::unique_ptr<VkFrameGraphBackend> frameGraphBackend;
    std::unique_ptr<SceneRenderer> sceneRenderer;
    std::unique_ptr<ModelSelection> modelSelection;
    std::unique_ptr<GizmoController> gizmoController;
    std::unique_ptr<WireframeRenderer> wireframeRenderer;
    std::unique_ptr<FrameController> frameController;
};

