#include "RuntimeController.hpp"

#include "RenderContext.hpp"
#include "RenderSettingsManager.hpp"
#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RuntimeExecutionController.hpp"
#include "RuntimeInputController.hpp"
#include "RuntimeRenderController.hpp"
#include "render/RenderRuntime.hpp"
#include "render/WindowRuntimeState.hpp"

RuntimeController::~RuntimeController() = default;

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

    runtimeInputController = std::make_unique<RuntimeInputController>(windowRuntimeState, *render.inputController());
    runtimeRenderController = std::make_unique<RuntimeRenderController>(
        *render.runtime(),
        render.sync(),
        allocator,
        settingsManager);
    runtimeExecutionController = std::make_unique<RuntimeExecutionController>(
        *runtimeInputController,
        *render.nodeGraphController(),
        *runtimeRenderController,
        scene.cameraController(),
        renderPaused);

    initialized = true;
    return true;
}

void RuntimeController::shutdown() {
    runtimeExecutionController.reset();
    runtimeRenderController.reset();
    runtimeInputController.reset();
    initialized = false;
}

bool RuntimeController::isInitialized() const {
    return initialized;
}

void RuntimeController::tick(float deltaTime, uint32_t& frameCounter) {
    if (runtimeExecutionController) {
        runtimeExecutionController->tick(deltaTime, frameCounter);
    }
}

bool RuntimeController::hasLastFrameSlot() const {
    return runtimeExecutionController && runtimeExecutionController->hasLastFrameSlot();
}

uint32_t RuntimeController::lastFrameSlot() const {
    if (!runtimeExecutionController) {
        return 0;
    }
    return runtimeExecutionController->lastFrameSlot();
}
