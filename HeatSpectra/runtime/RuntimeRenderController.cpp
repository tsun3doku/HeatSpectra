#include "RuntimeRenderController.hpp"

#include "heat/HeatSystemComputeController.hpp"
#include "RenderSettingsManager.hpp"
#include "framegraph/ComputePass.hpp"
#include "framegraph/FrameSync.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"

namespace {

render::RenderFlags buildRenderFlags(const app::RenderSettings& settings) {
    render::RenderFlags flags{};
    flags.wireframeMode = static_cast<int>(settings.wireframeMode);
    flags.drawTimingOverlay = settings.gpuTimingOverlayEnabled;
    return flags;
}

}


RuntimeRenderController::RuntimeRenderController(RenderRuntime& renderRuntime, FrameSync& frameSync, MemoryAllocator* memoryAllocator, RenderSettingsManager& settingsManager, HeatSystemComputeController* heatSystemComputeController)
    : renderRuntime(renderRuntime),
      frameSync(frameSync),
      memoryAllocator(memoryAllocator),
      settingsManager(settingsManager),
      heatSystemComputeController(heatSystemComputeController) {
}

RuntimeRenderFrameResult RuntimeRenderController::renderFrame(bool allowHeatSolve, uint32_t& frameCounter) {
    const app::RenderSettings settings = settingsManager.getSnapshot();
    const render::RenderFlags flags = buildRenderFlags(settings);
    std::vector<ComputePass*> computePasses;
    if (allowHeatSolve && heatSystemComputeController) {
        computePasses = heatSystemComputeController->getActiveSystems();
    }
    RuntimeRenderFrameResult result{};
    result.frameSlot = frameSync.getCurrentFrameIndex();
    result.submitted = true;
    renderRuntime.renderFrame(flags, computePasses);

    ++frameCounter;
    if (memoryAllocator &&
        frameCounter % renderconfig::AllocatorDefragIntervalFrames == 0) {
        memoryAllocator->defragment();
    }

    return result;
}
