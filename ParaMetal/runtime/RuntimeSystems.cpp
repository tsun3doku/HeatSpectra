#include "RuntimeSystems.hpp"

#include "render/WindowRuntimeState.hpp"
#include "render/SceneRenderer.hpp"
#include "render/HeatOverlayRenderer.hpp"
#include "scene/InputController.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

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
    frameCounter = 0;
    timelineRuntime.reset();

    if (!core.initialize(vulkanContext)) {
        cleanup();
        return false;
    }
    if (!scene.initialize(core)) {
        cleanup();
        return false;
    }
    if (!render.initialize(core, scene, runtimeState, vulkanContext, runtimeBusy, isShuttingDown)) {
        cleanup();
        return false;
    }
    timelineControllerInstance.bindPlaybackTarget(render.heatSystemComputeController());
    if (!render.initializeSyncObjects()) {
        cleanup();
        return false;
    }
    if (!runtimeController.initialize(
            render, scene, core, runtimeState)) {
        cleanup();
        return false;
    }

    initialized = true;
    return true;
}

void RuntimeSystems::tickFrame(float deltaTime, VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!initialized || !windowRuntimeState || !runtimeController.isInitialized()) {
        return;
    }
    if (isShuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    const bool heatActive = isSimulationActive();
    const uint32_t heatFrameCount = getSimulationTimelineFrameCount();
    if (heatActive && heatFrameCount > 0) {
        timelineRuntime.setFrameCount(heatFrameCount + 1u);
        const float heatDuration = getSimulationDuration();
        if (heatDuration > 0.0f) {
            timelineRuntime.setFps(static_cast<float>(heatFrameCount) / heatDuration);
        }

        const uint32_t rewindFrame = getSimulationRewindFrame();
        uint32_t syncedFrame = 0;
        if (rewindFrame != UINT32_MAX) {
            syncedFrame = rewindFrame;
        } else {
            const float fps = timelineRuntime.getFps();
            syncedFrame = fps > 0.0f
                ? static_cast<uint32_t>(std::round(getSimulationTotalTime() * fps))
                : 0u;
        }
        timelineRuntime.setCurrentFrame(syncedFrame);
        const bool heatAtEnd = timelineRuntime.getCurrentFrame() >= timelineRuntime.getMaxFrame() ||
            (heatDuration > 0.0f && getSimulationTotalTime() >= heatDuration);
        timelineRuntime.setPlaying(!isSimulationPaused() && !heatAtEnd);
    } else {
        timelineRuntime.tick(deltaTime);
    }

    runtimeController.tick(
        deltaTime,
        frameCounter,
        commandBuffer,
        frameIndex,
        renderSettingsState);
    dispatchViewportCommands();
}

bool RuntimeSystems::updateViewportTarget(VkImage image, VkFormat format, VkExtent2D extent) {
    if (!initialized) {
        return false;
    }
    return render.updateViewportTarget(image, format, extent);
}

void RuntimeSystems::replaceGraphState(const NodeGraphState& state) {
    if (NodeGraphController* controller = getNodeGraphController()) controller->resetGraph(state);
}

bool RuntimeSystems::applyGraphDelta(const NodeGraphDelta& delta) {
    NodeGraphController* controller = getNodeGraphController();
    return controller && controller->applyGraphDelta(delta);
}

void RuntimeSystems::dispatchViewportCommands() {
    for (ViewportCommand command : runtimeController.takePendingViewportCommands()) {
        switch (command) {
        case ViewportCommand::ToggleWireframe:
            renderSettingsState.wireframeMode = static_cast<app::WireframeMode>(
                (static_cast<int>(renderSettingsState.wireframeMode) + 1) % 3);
            break;
        case ViewportCommand::ToggleTimingOverlay:
            renderSettingsState.gpuTimingOverlayEnabled =
                !renderSettingsState.gpuTimingOverlayEnabled;
            break;
        case ViewportCommand::ToggleGrid:
            renderSettingsState.gridEnabled = !renderSettingsState.gridEnabled;
            break;
        }
    }
}

bool RuntimeSystems::takePendingNodeParameters(
    NodeGraphNodeId& outNodeId,
    std::vector<NodeGraphParamValue>& outParameters) {
    return runtimeController.takePendingNodeParameters(outNodeId, outParameters);
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

bool RuntimeSystems::getSerialTemperatureStatus(
    uint64_t sourceKey, SerialTemperatureRuntime::Status& outStatus) const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    return controller && controller->getSerialTemperatureStatus(sourceKey, outStatus);
}

TimelineController* RuntimeSystems::timelineController() {
    return &timelineControllerInstance;
}

const TimelineController* RuntimeSystems::timelineController() const {
    return &timelineControllerInstance;
}

void RuntimeSystems::setPanSensitivity(float sensitivity) {
    scene.cameraController().setPanSensitivity(sensitivity);
}

void RuntimeSystems::setWireframeMode(app::WireframeMode mode) {
    renderSettingsState.wireframeMode = mode;
}

void RuntimeSystems::setGridEnabled(bool enabled) {
    renderSettingsState.gridEnabled = enabled;
}

void RuntimeSystems::setWorldUnit(int unit) {
    if (unit < static_cast<int>(units::LengthUnit::Millimeter) ||
        unit > static_cast<int>(units::LengthUnit::Meter)) return;
    const units::LengthUnit next = static_cast<units::LengthUnit>(unit);
    if (next == worldUnitState) return;
    worldUnitState = next;
    if (NodeGraphController* controller = getNodeGraphController()) {
        controller->setWorldUnit(next);
    }
    scene.cameraController().setWorldUnit(next);
    if (render.runtime()) {
        render.runtime()->getSceneRenderer().setWorldUnit(next);
    }
}

void RuntimeSystems::setHeatPaletteRange(float minimum, float maximum) {
    if (render.runtime()) render.runtime()->getSceneRenderer().getHeatOverlayRenderer()->setPaletteRange(minimum, maximum);
}
void RuntimeSystems::setHeatPalette(int palette) {
    if (render.runtime()) render.runtime()->getSceneRenderer().getHeatOverlayRenderer()->setPalette(palette);
}

bool RuntimeSystems::isHeatPaletteVisible() const {
    const RenderRuntime* runtimeState = render.runtime();
    return runtimeState && runtimeState->getSceneRenderer().getHeatOverlayRenderer()->isPaletteVisible();
}

const app::RenderSettings& RuntimeSystems::renderSettings() const {
    return renderSettingsState;
}

CameraController* RuntimeSystems::getCameraController() {
    return &scene.cameraController();
}

const CameraController* RuntimeSystems::getCameraController() const {
    return &scene.cameraController();
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

NodeGraphController* RuntimeSystems::getNodeGraphController() {
    return render.nodeGraphController();
}

const NodeGraphController* RuntimeSystems::getNodeGraphController() const {
    return render.nodeGraphController();
}

void RuntimeSystems::cleanup() {
    timelineControllerInstance.bindPlaybackTarget(nullptr);
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

float RuntimeSystems::getSimulationTotalTime() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return 0.0f;
    float total = 0.0f;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            total = std::max(total, system->getSimulationTimeSeconds());
        }
    }
    return total;
}

uint32_t RuntimeSystems::getSimulationRecordedTimelineFrames() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return 0;
    uint32_t maxRecorded = 0;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            maxRecorded = std::max(maxRecorded, system->getRecordedTimelineFrames());
        }
    }
    return maxRecorded;
}

uint32_t RuntimeSystems::getSimulationTimelineFrameCount() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return 0;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            return system->getTimelineFrameCount();
        }
    }
    return 0;
}

float RuntimeSystems::getSimulationDuration() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return 0.0f;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            return system->getSimulationDurationSeconds();
        }
    }
    return 0.0f;
}

uint32_t RuntimeSystems::getSimulationResetCounter() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return 0;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            return system->getResetCounter();
        }
    }
    return 0;
}

uint32_t RuntimeSystems::getSimulationRewindFrame() const {
    const HeatSystemComputeController* controller = render.heatSystemComputeController();
    if (!controller) return UINT32_MAX;
    for (ComputePass* pass : controller->getActiveSystems()) {
        const HeatSystem* system = dynamic_cast<const HeatSystem*>(pass);
        if (system && system->getIsActive()) {
            return system->getRewindFrame();
        }
    }
    return UINT32_MAX;
}

bool RuntimeSystems::isTimelinePlaying() const {
    return timelineRuntime.isPlaying();
}

uint32_t RuntimeSystems::getTimelineCurrentFrame() const {
    return timelineRuntime.getCurrentFrame();
}

uint32_t RuntimeSystems::getTimelineFrameCount() const {
    return timelineRuntime.getFrameCount();
}

uint32_t RuntimeSystems::getTimelineStartDisplayFrame() const {
    return timelineRuntime.getStartDisplayFrame();
}

uint32_t RuntimeSystems::getTimelineEndDisplayFrame() const {
    return timelineRuntime.getEndDisplayFrame();
}

float RuntimeSystems::getTimelineCurrentSeconds() const {
    return timelineRuntime.getCurrentSeconds();
}

float RuntimeSystems::getTimelineDurationSeconds() const {
    return timelineRuntime.getDurationSeconds();
}
