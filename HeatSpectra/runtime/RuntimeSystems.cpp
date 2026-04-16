#include "RuntimeSystems.hpp"

#include "render/WindowRuntimeState.hpp"

RuntimeSystems::~RuntimeSystems() {
    shutdown();
}

bool RuntimeSystems::initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext) {
    if (initialized) {
        return true;
    }

    windowRuntimeState = &runtimeState;
    isShuttingDown.store(false, std::memory_order_release);
    runtimeBusy.store(false, std::memory_order_release);
    renderPaused.store(false, std::memory_order_release);
    frameCounter = 0;

    if (!core.initialize(vulkanContext)) {
        cleanup();
        return false;
    }
    if (!scene.initialize(core)) {
        cleanup();
        return false;
    }
    if (!render.initialize(core, scene, runtimeState, &settingsController, runtimeBusy, isShuttingDown)) {
        cleanup();
        return false;
    }
    if (!render.initializeSyncObjects()) {
        cleanup();
        return false;
    }
    if (!runtimeController.initialize(render, scene, core, runtimeState, settingsManager, renderPaused)) {
        cleanup();
        return false;
    }

    initialized = true;
    return true;
}

void RuntimeSystems::tickFrame(float deltaTime) {
    if (!initialized || !windowRuntimeState || !runtimeController.isInitialized()) {
        return;
    }
    if (isShuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    runtimeController.tick(deltaTime, frameCounter);
}

void RuntimeSystems::shutdown() {
    if (!initialized && !core.isInitialized() && !scene.isInitialized() && !render.isInitialized() &&
        !runtimeController.isInitialized() && !windowRuntimeState) {
        return;
    }

    isShuttingDown.store(true, std::memory_order_release);
    cleanup();
    runtimeBusy.store(false, std::memory_order_release);
    initialized = false;
}

bool RuntimeSystems::isInitialized() const {
    return initialized;
}

const RuntimeQuery* RuntimeSystems::runtimeQuery() const {
    return this;
}

std::vector<SimulationError> RuntimeSystems::consumeSimulationErrors() {
    return {};
}

uint32_t RuntimeSystems::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    SceneController* sceneController = render.sceneController();
    if (!sceneController) {
        return 0;
    }
    return sceneController->loadModel(modelPath, preferredModelId);
}

void RuntimeSystems::setPanSensitivity(float sensitivity) {
    scene.cameraController().setPanSensitivity(sensitivity);
}

void RuntimeSystems::setRenderPaused(bool paused) {
    renderPaused.store(paused, std::memory_order_release);
    if (paused && core.isInitialized() && core.device().getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(core.device().getDevice());
    }
}

RenderSettingsController* RuntimeSystems::getSettingsController() {
    return &settingsController;
}

const RenderSettingsController* RuntimeSystems::getSettingsController() const {
    return &settingsController;
}

NodeGraphBridge* RuntimeSystems::getNodeGraphBridge() {
    return render.nodeGraphBridge();
}

const NodeGraphBridge* RuntimeSystems::getNodeGraphBridge() const {
    return render.nodeGraphBridge();
}

SceneController* RuntimeSystems::getSceneController() {
    return render.sceneController();
}

const SceneController* RuntimeSystems::getSceneController() const {
    return render.sceneController();
}

ModelSelection* RuntimeSystems::getModelSelection() {
    RenderRuntime* renderRuntime = render.runtime();
    if (!renderRuntime) {
        return nullptr;
    }
    return &renderRuntime->getModelSelection();
}

const ModelSelection* RuntimeSystems::getModelSelection() const {
    const RenderRuntime* renderRuntime = render.runtime();
    if (!renderRuntime) {
        return nullptr;
    }
    return &renderRuntime->getModelSelection();
}

void RuntimeSystems::cleanup() {
    runtimeController.shutdown();
    render.shutdown();
    scene.shutdown();
    core.shutdown();
    windowRuntimeState = nullptr;
}

bool RuntimeSystems::isSimulationActive() const {
    const HeatSystemComputeController* heatSystemController = render.heatSystemComputeController();
    return heatSystemController && heatSystemController->isAnyHeatSystemActive();
}

bool RuntimeSystems::isSimulationPaused() const {
    const HeatSystemComputeController* heatSystemController = render.heatSystemComputeController();
    return heatSystemController && heatSystemController->isAnyHeatSystemPaused();
}
