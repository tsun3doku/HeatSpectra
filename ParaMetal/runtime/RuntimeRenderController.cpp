#include "RuntimeRenderController.hpp"

#include "heat/HeatSystemComputeController.hpp"
#include "framegraph/ComputePass.hpp"
#include "framegraph/FrameSync.hpp"
#include "render/RenderConfig.hpp"
#include "render/RenderRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"

render::RenderFlags buildRenderFlags(const app::RenderSettings& settings) {
    render::RenderFlags flags{};
    flags.wireframeMode = static_cast<int>(settings.wireframeMode);
    flags.drawTimingOverlay = settings.gpuTimingOverlayEnabled;
    flags.drawGrid = settings.gridEnabled;
    flags.backgroundMode = static_cast<int>(settings.backgroundMode);
    flags.drawNavigationCube = settings.navigationCubeVisible;
    flags.drawAxisLabels = settings.axisLabelsVisible;
    return flags;
}

RuntimeRenderController::RuntimeRenderController(RenderRuntime& renderRuntime, FrameSync& frameSync, MemoryAllocator* memoryAllocator, HeatSystemComputeController* heatSystemComputeController)
    : renderRuntime(renderRuntime),
      frameSync(frameSync),
      memoryAllocator(memoryAllocator),
      heatSystemComputeController(heatSystemComputeController) {
}

RuntimeRenderFrameResult RuntimeRenderController::renderFrame(
    bool allowHeatSolve,
    uint32_t& frameCounter,
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    const app::RenderSettings& settings) {
    const render::RenderFlags flags = buildRenderFlags(settings);
    std::vector<ComputePass*> computePasses;
    if (heatSystemComputeController) {
        heatSystemComputeController->updateSerialInputs();
        if (allowHeatSolve) {
            computePasses = heatSystemComputeController->getActiveSystems();
        }
    }
    RuntimeRenderFrameResult result{};
    result.frameSlot = frameIndex;
    result.submitted = renderRuntime.renderFrame(commandBuffer, frameIndex, flags, computePasses);

    ++frameCounter;

    return result;
}
