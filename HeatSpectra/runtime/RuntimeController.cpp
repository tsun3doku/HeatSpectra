#include "RuntimeController.hpp"

#include "RenderContext.hpp"
#include "SceneContext.hpp"
#include "VulkanCoreContext.hpp"
#include "RuntimeExecutionController.hpp"
#include "RuntimeInputController.hpp"
#include "RuntimeRenderController.hpp"
#include "RuntimeSimulationController.hpp"
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

    if (!render.heatSystemController() || !render.modelRuntime() || !render.sceneController() || !render.nodeGraphBridge() || !render.nodeGraphController()) {
        return false;
    }

    runtimeSimulationController = std::make_unique<RuntimeSimulationController>(
        *render.heatSystemController(),
        *render.modelRuntime(),
        *render.nodeGraphController(),
        settingsManager);

    if (!runtimeSimulationController || !render.initializeInputPipeline(scene, *runtimeSimulationController) || !render.runtime() ||
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
        *runtimeSimulationController,
        scene.cameraController(),
        renderPaused);

    initialized = true;
    return true;
}

void RuntimeController::shutdown() {
    runtimeExecutionController.reset();
    runtimeRenderController.reset();
    runtimeInputController.reset();
    runtimeSimulationController.reset();
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

const RuntimeQuery* RuntimeController::runtimeQuery() const {
    return runtimeSimulationController.get();
}

RuntimeSimulationController* RuntimeController::simulationController() {
    return runtimeSimulationController.get();
}

const RuntimeSimulationController* RuntimeController::simulationController() const {
    return runtimeSimulationController.get();
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
