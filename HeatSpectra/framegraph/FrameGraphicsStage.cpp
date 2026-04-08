#include "FrameGraphicsStage.hpp"

#include <iostream>

#include "FramePass.hpp"
#include "FrameSync.hpp"
#include "contact/ContactSystem.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/VoronoiSystem.hpp"
#include "mesh/MeshModifiers.hpp"
#include "render/SceneRenderer.hpp"
#include "vulkan/VulkanDevice.hpp"

FrameGraphicsStage::FrameGraphicsStage(VulkanDevice& vulkanDevice, FrameSync& frameSync, SceneRenderer& sceneRenderer, MeshModifiers& meshModifiers,
    ModelSelection& modelSelection, GizmoController& gizmoController, WireframeRenderer& wireframeRenderer)
    : vulkanDevice(vulkanDevice),
      frameSync(frameSync),
      sceneRenderer(sceneRenderer),
      meshModifiers(meshModifiers),
      modelSelection(modelSelection),
      gizmoController(gizmoController),
      wireframeRenderer(wireframeRenderer) {
}

FrameStageResult FrameGraphicsStage::execute(const FrameState& frameState, const std::vector<HeatSystem*>& heatSystems, const std::vector<VoronoiSystem*>& voronoiSystems, const std::vector<ContactSystem*>& contactSystems, const FrameSyncState& syncState, bool allowHeatSolve) {
    const auto& commandBuffers = sceneRenderer.getCommandBuffers();
    if (frameState.frameIndex >= commandBuffers.size()) {
        std::cout << "[FrameGraphicsStage] Missing scene renderer command buffer for frame index" << std::endl;
        return FrameStageResult::Fatal;
    }

    VkCommandBuffer commandBuffer = commandBuffers[frameState.frameIndex];
    vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    frameSync.prepareGraphicsSubmit();

    render::RenderFrameRequest frameRequest{};
    frameRequest.frameIndex = frameState.frameIndex;
    frameRequest.imageIndex = frameState.imageIndex;
    frameRequest.extent = frameState.extent;
    frameRequest.sceneView = frameState.sceneView;
    frameRequest.flags = frameState.flags;
    (void)allowHeatSolve;
    frameRequest.overlay = frameState.overlay;

    render::RenderServices services{};
    services.heatSystems = heatSystems;
    services.voronoiSystems = voronoiSystems;
    services.contactSystems = contactSystems;
    services.modelSelection = &modelSelection;
    services.gizmoController = &gizmoController;
    services.wireframeRenderer = &wireframeRenderer;

    if (!sceneRenderer.recordCommandBuffer(
        frameRequest,
        services,
        syncState.insertComputeToGraphicsBarrier,
        syncState.waitDstStageMask)) {
        std::cout << "[FrameGraphicsStage] Scene command recording failed. Triggering swapchain recreation." << std::endl;
        return FrameStageResult::RecreateSwapchain;
    }

    const VkResult submitResult = frameSync.submitGraphics(
        vulkanDevice.getGraphicsQueue(),
        commandBuffer,
        syncState.waitForComputeSemaphore,
        syncState.waitDstStageMask);

    if (submitResult != VK_SUCCESS) {
        std::cout << "[FrameGraphicsStage] vkQueueSubmit FAILED with result=" << submitResult;
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            std::cout << " (VK_ERROR_DEVICE_LOST). Treating as fatal." << std::endl;
            return FrameStageResult::Fatal;
        }
        std::cout << " Triggering swapchain recreation" << std::endl;
        return FrameStageResult::RecreateSwapchain;
    }

    return FrameStageResult::Continue;
}
