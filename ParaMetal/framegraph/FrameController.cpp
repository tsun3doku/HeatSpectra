#include "FrameController.hpp"

#include "scene/CameraController.hpp"
#include "scene/NavigationGizmoController.hpp"
#include "FrameStats.hpp"
#include "FrameSync.hpp"
#include "render/SceneRenderer.hpp"
#include "app/ViewportTarget.hpp"
#include "VkFrameGraphBackend.hpp"
#include "framegraph/ComputePass.hpp"
#include "render/RenderConfig.hpp"
#include "render/WindowRuntimeState.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <optional>
#include <utility>

FrameController::FrameController(
    const WindowRuntimeState& windowState,
    VulkanDevice& vulkanDevice,
    ViewportTarget& viewportTarget,
    FrameGraph& frameGraph,
    VkFrameGraphBackend& frameGraphBackend,
    SceneRenderer& sceneRenderer,
    FrameSync& frameSync,
    FrameStats& frameStats,
    CameraController& cameraController,
    FrameControllerServices services,
    std::atomic<bool>& isOperating,
    std::atomic<bool>& isShuttingDown)
    : windowState(windowState),
      viewportTarget(viewportTarget),
      sceneRenderer(sceneRenderer),
      frameSync(frameSync),
      frameStats(frameStats),
      cameraController(cameraController),
      navigationGizmoController(services.navigationGizmoController),
      isOperating(isOperating),
      isShuttingDown(isShuttingDown),
      vulkanDevice(vulkanDevice),
      frameUpdateStage(
          services.inputController,
          services.uniformBufferManager,
          services.modelRegistry,
          services.lightingSystem,
          services.materialSystem,
          sceneRenderer,
          services.modelSelection),
      frameComputeStage(
          vulkanDevice,
          frameGraphBackend.getRuntime(),
          frameSync),
      frameGraphicsStage(
          vulkanDevice,
          frameSync,
          sceneRenderer,
          services.modelSelection,
          services.gizmoController,
          services.navigationGizmoController,
          services.wireframeRenderer) {
}

bool FrameController::drawFrame(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    const render::RenderFlags& flags,
    const std::vector<ComputePass*>& computePasses) {
    if (isShuttingDown.load(std::memory_order_acquire) || isOperating.load(std::memory_order_acquire)) {
        return false;
    }

    const VkExtent2D extent = viewportTarget.getExtent();
    if (commandBuffer == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) {
        return false;
    }

    frameSync.prepareExternalFrame(frameIndex);

    FrameState frameState{};
    frameState.frameIndex = frameIndex;
    std::vector<std::string> timingLines;
    frameUpdateStage.processPicking(frameState.frameIndex);

    frameState.extent = extent;
    navigationGizmoController.setViewport(
        frameState.extent,
        windowState.devicePixelRatio.load(std::memory_order_acquire));
    frameState.sceneView = cameraController.buildSceneView(frameState.extent);
    frameState.flags = flags;
    frameUpdateStage.updateFrameState(frameState.frameIndex, frameState.sceneView);

    const FrameComputeCollection computeCollection = frameComputeStage.collect(frameState.frameIndex, computePasses);
    if (computeCollection.result != FrameStageResult::Continue) {
        if (computeCollection.result == FrameStageResult::Fatal) {
            isShuttingDown.store(true, std::memory_order_release);
        }
        return false;
    }

    if (flags.drawTimingOverlay) {
        timingLines = buildFrameTimingLines(frameState.frameIndex, computeCollection.computeGpuMs);
    }

    FrameSyncState syncState{};
    syncState.waitForComputeSemaphore = computeCollection.waitForComputeSemaphore;
    syncState.insertComputeToGraphicsBarrier = computeCollection.insertComputeToGraphicsBarrier;
    syncState.waitDstStageMask = computeCollection.waitDstStageMask;

    updateTimingOverlay(timingLines, frameState.flags);

    if (computeCollection.commandBuffer != VK_NULL_HANDLE) {
        FrameSync::FrameSubmission computeSubmission{};
        computeSubmission.computeCommandBuffer = computeCollection.commandBuffer;
        computeSubmission.externalWaitSemaphore = computeCollection.synchronization.waitSemaphore;
        computeSubmission.externalWaitValue = computeCollection.synchronization.waitValue;
        computeSubmission.externalSignalSemaphore = computeCollection.synchronization.signalSemaphore;
        computeSubmission.externalSignalValue = computeCollection.synchronization.signalValue;
        if (frameSync.submitCompute(vulkanDevice.getComputeQueue(), computeSubmission) != VK_SUCCESS) {
            isShuttingDown.store(true, std::memory_order_release);
            return false;
        }
    }

    const FrameGraphicsCollection graphicsCollection =
        frameGraphicsStage.collectExternal(commandBuffer, frameState, syncState);
    if (graphicsCollection.result != FrameStageResult::Continue) {
        isShuttingDown.store(true, std::memory_order_release);
        return false;
    }

    return true;
}

std::vector<std::string> FrameController::buildFrameTimingLines(uint32_t frameIndex, std::optional<float> computeGpuMs) {
    GpuTimingStats gpuTiming{};
    const GpuTimingStats* graphicsTiming = nullptr;
    if (sceneRenderer.getGpuTimingStats(frameIndex, gpuTiming)) {
        graphicsTiming = &gpuTiming;
    }

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
