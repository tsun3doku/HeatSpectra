#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "RuntimeInputController.hpp"
#include "RuntimeRenderController.hpp"

class RenderContext;
class RenderSettingsManager;
class SceneContext;
class VulkanCoreContext;
class NodeGraphController;
class CameraController;
struct WindowRuntimeState;

class RuntimeController {
public:
    ~RuntimeController();

    bool initialize(RenderContext& render, SceneContext& scene, VulkanCoreContext& core,
                    WindowRuntimeState& windowRuntimeState, RenderSettingsManager& settingsManager,
                    std::atomic<bool>& renderPaused);
    void shutdown();
    bool isInitialized() const;
    void tick(float deltaTime, uint32_t& frameCounter);

    bool hasLastFrameSlot() const;
    uint32_t lastFrameSlot() const;

private:
    RuntimeInputController& inputController();
    NodeGraphController* nodeGraphController();
    CameraController* cameraController();

    std::unique_ptr<RuntimeInputController> runtimeInputController;
    std::unique_ptr<RuntimeRenderController> runtimeRenderController;
    RenderContext* render = nullptr;
    SceneContext* scene = nullptr;
    std::atomic<bool>* renderPaused = nullptr;
    bool hasFrameSlot = false;
    uint32_t frameSlot = 0;
    bool initialized = false;
};