#pragma once

#include <vulkan/vulkan.h>

#include "FramePass.hpp"
#include "scene/SceneView.hpp"

enum class FrameStageResult {
    Continue,
    SkipFrame,
    RecreateSwapchain,
    Fatal
};

struct FrameState {
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    VkExtent2D extent{};
    render::SceneView sceneView{};
    render::RenderFlags flags{};
    render::OverlayParams overlay{};
};

struct FrameSyncState {
    bool waitForComputeSemaphore = false;
    bool insertComputeToGraphicsBarrier = false;
    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
};
