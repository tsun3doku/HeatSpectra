#pragma once

#include <cstdint>

#include "FrameTypes.hpp"

class VulkanDevice;
class FrameSync;
class SceneRenderer;
class ModelSelection;
class GizmoController;
class WireframeRenderer;

class FrameGraphicsStage {
public:
    FrameGraphicsStage(
        VulkanDevice& vulkanDevice,
        FrameSync& frameSync,
        SceneRenderer& sceneRenderer,
        ModelSelection& modelSelection,
        GizmoController& gizmoController,
        WireframeRenderer& wireframeRenderer);

    FrameStageResult execute(const FrameState& frameState, const FrameSyncState& syncState);

private:
    VulkanDevice& vulkanDevice;
    FrameSync& frameSync;
    SceneRenderer& sceneRenderer;
    ModelSelection& modelSelection;
    GizmoController& gizmoController;
    WireframeRenderer& wireframeRenderer;
};
