#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class HeatSystemComputeController;
class HeatSystemDisplayController;
class ContactSystemComputeController;
class VoronoiSystemComputeController;
class ContactSystem;

#include "FrameComputeStage.hpp"
#include "FrameGraphicsStage.hpp"
#include "FrameTypes.hpp"
#include "FrameUpdateStage.hpp"
#include "app/SwapchainStage.hpp"

struct WindowRuntimeState;
class VulkanDevice;
class SwapchainManager;
class FrameGraph;
class SceneRenderer;
class FrameSync;
class ComputeTiming;
class FrameStats;
class CameraController;
class ModelRegistry;
class MeshModifiers;
class UniformBufferManager;
class ModelSelection;
class GizmoController;
class WireframeRenderer;
class VkFrameGraphBackend;
class InputController;
class LightingSystem;
class MaterialSystem;

struct FrameControllerServices {
    ModelRegistry& resourceManager;
    MeshModifiers& meshModifiers;
    UniformBufferManager& uniformBufferManager;
    HeatSystemComputeController* heatSystemComputeController = nullptr;
    HeatSystemDisplayController* heatSystemDisplayController = nullptr;
    ContactSystemComputeController* contactSystemController = nullptr;
    VoronoiSystemComputeController* voronoiSystemComputeController = nullptr;
    ModelSelection& modelSelection;
    GizmoController& gizmoController;
    WireframeRenderer& wireframeRenderer;
    InputController& inputController;
    LightingSystem& lightingSystem;
    MaterialSystem& materialSystem;
};

class FrameController {
public:
    FrameController(
        const WindowRuntimeState& windowState,
        VulkanDevice& vulkanDevice,
        SwapchainManager& swapchainManager,
        FrameGraph& frameGraph,
        VkFrameGraphBackend& frameGraphBackend,
        SceneRenderer& sceneRenderer,
        FrameSync& frameSync,
        ComputeTiming& computeTiming,
        FrameStats& frameStats,
        CameraController& cameraController,
        FrameControllerServices services,
        std::atomic<bool>& isOperating,
        std::atomic<bool>& isShuttingDown);

    bool initializeSyncObjects();
    void shutdownSyncObjects();
    void cleanupSwapChain();
    bool recreateSwapChain();
    void drawFrame(const render::RenderFlags& flags, bool allowHeatSolve);

private:
    const WindowRuntimeState& windowState;
    SwapchainManager& swapchainManager;
    SceneRenderer& sceneRenderer;
    FrameSync& frameSync;
    ComputeTiming& computeTiming;
    FrameStats& frameStats;
    CameraController& cameraController;
    HeatSystemComputeController* heatSystemComputeController = nullptr;
    HeatSystemDisplayController* heatSystemDisplayController = nullptr;
    ContactSystemComputeController* contactSystemController = nullptr;
    VoronoiSystemComputeController* voronoiSystemComputeController = nullptr;
    std::atomic<bool>& isOperating;
    std::atomic<bool>& isShuttingDown;

    SwapchainStage swapchainStage;
    FrameUpdateStage frameUpdateStage;
    FrameComputeStage frameComputeStage;
    FrameGraphicsStage frameGraphicsStage;

    std::vector<std::string> buildFrameTimingLines(uint32_t frameIndex);
    void updateTimingOverlay(std::vector<std::string>& timingLines, const render::RenderFlags& flags);
    bool handleStageResult(FrameStageResult result);
};

