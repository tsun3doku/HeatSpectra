#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "util/ComputeTiming.hpp"
#include "framegraph/ComputePass.hpp"
#include "framegraph/FrameStats.hpp"
#include "framegraph/FramePass.hpp"
#include "WindowRuntimeState.hpp"

class VulkanDevice;
class SwapchainManager;
class CommandPool;
class FrameSync;
class CameraController;
class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
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

struct FrameControllerServices;

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

    bool initializeBase(VkFormat swapChainFormat, VkExtent2D extent, MemoryAllocator& allocator, ModelRegistry& resourceManager, UniformBufferManager& ubo);
    bool initializeFrameController(const FrameControllerServices& services);

    bool initializeSyncObjects();
    void shutdownSyncObjects();
    void renderFrame(const render::RenderFlags& flags, const std::vector<ComputePass*>& computePasses);
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
    WireframeRenderer& getWireframeRenderer();
    const WireframeRenderer& getWireframeRenderer() const;

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

