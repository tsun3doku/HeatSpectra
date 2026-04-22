#include "RuntimeController.hpp"

#include "RenderContext.hpp"
#include "RenderSettingsManager.hpp"
#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/CameraController.hpp"

RuntimeController::~RuntimeController() {
    shutdown();
}

bool RuntimeController::initialize(
    RenderContext& render,
    SceneContext& scene,
    VulkanCoreContext& core,
    WindowRuntimeState& windowRuntimeState,
    RenderSettingsManager& settingsManager,
    std::atomic<bool>& renderPaused) {
    if (initialized) {
        return true;
    }

    if (!render.heatSystemComputeController() || !render.modelComputeRuntime() || !render.sceneController() || !render.nodeGraphBridge() || !render.nodeGraphController()) {
        return false;
    }

    if (!render.initializeInputPipeline(scene, settingsManager) || !render.runtime() ||
        !render.inputController()) {
        shutdown();
        return false;
    }

    auto* allocator = core.allocator();
    if (!allocator) {
        shutdown();
        return false;
    }

    this->render = &render;
    this->scene = &scene;
    this->renderPaused = &renderPaused;

    runtimeInputController = std::make_unique<RuntimeInputController>(windowRuntimeState, *render.inputController());
    runtimeRenderController = std::make_unique<RuntimeRenderController>(
        *render.runtime(),
        render.sync(),
        allocator,
        settingsManager,
        render.heatSystemComputeController());

    initialized = true;
    return true;
}

void RuntimeController::shutdown() {
    runtimeRenderController.reset();
    runtimeInputController.reset();
    render = nullptr;
    scene = nullptr;
    renderPaused = nullptr;
    initialized = false;
}

bool RuntimeController::isInitialized() const {
    return initialized;
}

RuntimeInputController& RuntimeController::inputController() {
    return *runtimeInputController;
}

NodeGraphController* RuntimeController::nodeGraphController() {
    return render ? render->nodeGraphController() : nullptr;
}

CameraController* RuntimeController::cameraController() {
    return scene ? &scene->cameraController() : nullptr;
}

void RuntimeController::tick(float deltaTime, uint32_t& frameCounter) {
    if (!runtimeRenderController || !runtimeInputController || !render || !scene) {
        return;
    }

    hasFrameSlot = false;

    runtimeInputController->tick(deltaTime);
    render->nodeGraphController()->applyPendingChanges();
    if (renderPaused && renderPaused->load(std::memory_order_acquire)) {
        return;
    }

    render->nodeGraphController()->tick();
    scene->cameraController().tick(deltaTime);
    const bool allowHeatSolve = render->nodeGraphController()->canExecuteHeatSolve();
    const RuntimeRenderFrameResult renderResult = runtimeRenderController->renderFrame(allowHeatSolve, frameCounter);
    hasFrameSlot = renderResult.submitted;
    frameSlot = renderResult.frameSlot;
}

bool RuntimeController::hasLastFrameSlot() const {
    return hasFrameSlot;
}

uint32_t RuntimeController::lastFrameSlot() const {
    return frameSlot;
}