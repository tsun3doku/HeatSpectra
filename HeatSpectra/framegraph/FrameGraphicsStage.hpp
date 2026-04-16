#pragma once

#include <cstdint>

#include "FrameTypes.hpp"

class VulkanDevice;
class FrameSync;
class SceneRenderer;
class MeshModifiers;
class ModelSelection;
class GizmoController;
class WireframeRenderer;

class FrameGraphicsStage {
public:
    FrameGraphicsStage(
        VulkanDevice& vulkanDevice,
        FrameSync& frameSync,
        SceneRenderer& sceneRenderer,
        MeshModifiers& meshModifiers,
        ModelSelection& modelSelection,
        GizmoController& gizmoController,
        WireframeRenderer& wireframeRenderer);

    FrameStageResult execute(const FrameState& frameState, const FrameSyncState& syncState, bool allowHeatSolve);

private:
    VulkanDevice& vulkanDevice;
    FrameSync& frameSync;
    SceneRenderer& sceneRenderer;
    MeshModifiers& meshModifiers;
    ModelSelection& modelSelection;
    GizmoController& gizmoController;
    WireframeRenderer& wireframeRenderer;
};
