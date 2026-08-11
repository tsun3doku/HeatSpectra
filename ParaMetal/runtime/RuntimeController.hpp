#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "RuntimeInput.hpp"
#include "RuntimeRenderController.hpp"

class RenderContext;
class SceneContext;
class VulkanCoreContext;
class NodeGraphController;
class CameraController;
struct WindowRuntimeState;

class RuntimeController {
public:
    ~RuntimeController();

    bool initialize(RenderContext& render, SceneContext& scene, VulkanCoreContext& core,
                    WindowRuntimeState& windowRuntimeState);
    void shutdown();
    bool isInitialized() const;
    void tick(
        float deltaTime,
        uint32_t& frameCounter,
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        const app::RenderSettings& renderSettings);

    bool hasLastFrameSlot() const;
    uint32_t lastFrameSlot() const;
    std::vector<ViewportCommand> takePendingViewportCommands();
    bool takePendingNodeParameters(
        NodeGraphNodeId& outNodeId,
        std::vector<NodeGraphParamValue>& outParameters);

private:
    NodeGraphController* nodeGraphController();
    CameraController* cameraController();

    std::unique_ptr<RuntimeInput> runtimeInput;
    std::unique_ptr<RuntimeRenderController> runtimeRenderController;
    RenderContext* render = nullptr;
    SceneContext* scene = nullptr;
    bool hasFrameSlot = false;
    uint32_t frameSlot = 0;
    bool initialized = false;
};
