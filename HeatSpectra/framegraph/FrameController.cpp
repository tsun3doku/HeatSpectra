#include "FrameController.hpp"

#include "scene/CameraController.hpp"
#include "util/ComputeTiming.hpp"
#include "FrameStats.hpp"
#include "FrameSync.hpp"
#include "render/SceneRenderer.hpp"
#include "app/SwapchainManager.hpp"
#include "VkFrameGraphBackend.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/VoronoiSystem.hpp"
#include "render/RenderConfig.hpp"
#include "render/WindowRuntimeState.hpp"

#include <optional>
#include <utility>

FrameController::FrameController(
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
    std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      swapchainManager(swapchainManager),
      sceneRenderer(sceneRenderer),
      frameSync(frameSync),
      computeTiming(computeTiming),
      frameStats(frameStats),
      cameraController(cameraController),
      heatSystem(services.heatSystem),
      voronoiSystem(services.voronoiSystem),
      isOperating(isOperating),
      isShuttingDown(isShuttingDown),
      swapchainStage(
          windowState,
          vulkanDevice,
          swapchainManager,
          frameGraph,
          frameGraphBackend,
          sceneRenderer,
          frameSync,
          heatSystem,
          voronoiSystem,
          isShuttingDown),
      frameUpdateStage(
          services.inputController,
          services.uniformBufferManager,
          services.resourceManager,
          services.lightingSystem,
          services.materialSystem,
          sceneRenderer,
          services.modelSelection),
      frameComputeStage(
          vulkanDevice,
          frameGraphBackend.getRuntime(),
          frameSync,
          computeTiming),
      frameGraphicsStage(
          vulkanDevice,
          frameSync,
          sceneRenderer,
          services.meshModifiers,
          services.modelSelection,
          services.gizmoController,
          services.wireframeRenderer) {
}

bool FrameController::initializeSyncObjects() {
    return swapchainStage.initializeSyncObjects();
}

void FrameController::shutdownSyncObjects() {
    swapchainStage.shutdownSyncObjects();
}

void FrameController::cleanupSwapChain() {
    swapchainStage.cleanupSwapChain();
}

bool FrameController::recreateSwapChain() {
    return swapchainStage.recreateSwapChain();
}

void FrameController::drawFrame(const render::RenderFlags& flags, const render::OverlayParams& overlay, bool allowHeatSolve) {
    if (isShuttingDown.load(std::memory_order_acquire) || isOperating.load(std::memory_order_acquire)) {
        return;
    }

    const VkExtent2D extent = swapchainManager.getExtent();
    const uint32_t targetWidth = windowState.width.load(std::memory_order_acquire);
    const uint32_t targetHeight = windowState.height.load(std::memory_order_acquire);
    if (targetWidth >= static_cast<uint32_t>(renderconfig::MinSwapchainExtent) &&
        targetHeight >= static_cast<uint32_t>(renderconfig::MinSwapchainExtent) &&
        (extent.width != targetWidth || extent.height != targetHeight)) {
        if (!recreateSwapChain()) {
            return;
        }
    }

    FrameState frameState{};
    frameState.frameIndex = frameSync.beginFrame();
    uint32_t imageIndex = 0;
    const FrameStageResult acquireResult = swapchainStage.acquireFrameImage(imageIndex);
    if (acquireResult == FrameStageResult::RecreateSwapchain) {
        (void)recreateSwapChain();
        return;
    }
    if (acquireResult != FrameStageResult::Continue) {
        return;
    }

    std::vector<std::string> timingLines = buildFrameTimingLines(frameState.frameIndex);
    frameUpdateStage.processPicking(frameState.frameIndex);

    frameState.extent = swapchainManager.getExtent();
    frameState.imageIndex = imageIndex;
    frameState.sceneView = cameraController.buildSceneView(frameState.extent);
    frameState.flags = flags;
    frameState.overlay = overlay;
    frameUpdateStage.updateFrameState(frameState.frameIndex, frameState.sceneView);

    FrameSyncState syncState{};
    if (!handleStageResult(frameComputeStage.execute(frameState.frameIndex, heatSystem, syncState, allowHeatSolve))) {
        return;
    }

    updateTimingOverlay(timingLines, frameState.flags);

    if (!handleStageResult(frameGraphicsStage.execute(frameState, heatSystem, voronoiSystem, syncState, allowHeatSolve))) {
        return;
    }

    const FrameStageResult presentResult = swapchainStage.presentFrame(frameState.imageIndex);
    if (presentResult == FrameStageResult::RecreateSwapchain) {
        (void)recreateSwapChain();
        return;
    }
    if (presentResult != FrameStageResult::Continue) {
        return;
    }

    frameSync.advanceFrame();
}

std::vector<std::string> FrameController::buildFrameTimingLines(uint32_t frameIndex) {
    GpuTimingStats gpuTiming{};
    const GpuTimingStats* graphicsTiming = nullptr;
    if (sceneRenderer.getGpuTimingStats(frameIndex, gpuTiming)) {
        graphicsTiming = &gpuTiming;
    }

    const std::optional<float> computeGpuMs = computeTiming.getGpuTimeMs(frameIndex);
    return frameStats.buildTimingLines(graphicsTiming, computeGpuMs);
}

void FrameController::updateTimingOverlay(std::vector<std::string>& timingLines, const render::RenderFlags& flags) {
    if (flags.drawTimingOverlay) {
        frameStats.capitalizeLines(timingLines);
        sceneRenderer.setTimingOverlayLines(timingLines);
        return;
    }

    sceneRenderer.setTimingOverlayLines({});
}

bool FrameController::handleStageResult(FrameStageResult result) {
    switch (result) {
    case FrameStageResult::Continue:
        return true;
    case FrameStageResult::SkipFrame:
        return false;
    case FrameStageResult::RecreateSwapchain:
        (void)recreateSwapChain();
        return false;
    case FrameStageResult::Fatal:
        return false;
    }

    return false;
}

