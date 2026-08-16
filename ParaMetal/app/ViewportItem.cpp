#include "ViewportItem.hpp"

#include "AppTypes.hpp"
#include "RuntimeNotifier.hpp"
#include "nodegraph/NodeGraphController.hpp"
#include "runtime/RuntimeInterfaces.hpp"
#include "runtime/RuntimeSystems.hpp"
#include "runtime/TimelineController.hpp"
#include "scene/Camera.hpp"
#include "scene/CameraController.hpp"
#include "scene/ModelSelection.hpp"

#include <QtCore/QElapsedTimer>
#include <QtGui/QFocusEvent>
#include <QtGui/QHoverEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtQuick/QQuickWindow>
#include <QtGui/6.9.3/QtGui/rhi/qrhi.h>
#include <QtGui/6.9.3/QtGui/rhi/qrhi_platform.h>

#include <algorithm>

class ViewportRenderer final : public QQuickRhiItemRenderer {
public:
    ViewportRenderer(RuntimeSystems& runtime, RuntimeNotifier& notifier, ViewportMailbox& mailbox)
        : runtime(runtime), notifier(notifier), mailbox(mailbox) {
    }

    ~ViewportRenderer() override {
        runtime.shutdown();
    }

protected:
    void initialize(QRhiCommandBuffer*) override {
        if (!rhi() || rhi()->backend() != QRhi::Vulkan || !colorTexture()) {
            return;
        }

        const QRhiVulkanNativeHandles* nativeHandles =
            static_cast<const QRhiVulkanNativeHandles*>(rhi()->nativeHandles());
        if (!nativeHandles ||
            nativeHandles->physDev == VK_NULL_HANDLE ||
            nativeHandles->dev == VK_NULL_HANDLE ||
            nativeHandles->gfxQueue == VK_NULL_HANDLE) {
            return;
        }

        const QRhiTexture::NativeTexture nativeTexture = colorTexture()->nativeTexture();
        const VkImage image = reinterpret_cast<VkImage>(nativeTexture.object);
        const QSize size = colorTexture()->pixelSize();
        const VkExtent2D extent{
            static_cast<uint32_t>(std::max(1, size.width())),
            static_cast<uint32_t>(std::max(1, size.height()))
        };

        WindowRuntimeState& state = mailbox.runtimeState();
        state.width.store(extent.width, std::memory_order_release);
        state.height.store(extent.height, std::memory_order_release);

        if (!runtime.isInitialized()) {
            AppVulkanContext context{};
            context.physicalDevice = nativeHandles->physDev;
            context.device = nativeHandles->dev;
            context.graphicsQueue = nativeHandles->gfxQueue;
            context.queueFamilyIndex = nativeHandles->gfxQueueFamilyIdx;
            context.viewportImage = image;
            context.viewportFormat = VK_FORMAT_R8G8B8A8_UNORM;
            context.viewportExtent = extent;
            initialized = runtime.initialize(state, context);
            if (initialized) {
                runtimeFresh = true;
                frameTimer.start();
            }
            return;
        }

        initialized = runtime.updateViewportTarget(
            image,
            VK_FORMAT_R8G8B8A8_UNORM,
            extent);
    }

    void synchronize(QQuickRhiItem* item) override {
        ViewportItem* viewportItem = static_cast<ViewportItem*>(item);
        viewportItemForSignals = viewportItem;
        WindowRuntimeState& state = mailbox.runtimeState();
        state.devicePixelRatio.store(
            static_cast<float>(viewportItem->window() ? viewportItem->window()->devicePixelRatio() : 1.0),
            std::memory_order_release);

        int worldUnit = static_cast<int>(units::defaultLengthUnit());
        if (mailbox.takeWorldUnit(worldUnit, runtimeFresh)) {
            runtime.setWorldUnit(worldUnit);
        }

        if (const NodeGraphState* graphState = mailbox.graphReplacement(runtimeFresh)) {
            runtime.replaceGraphState(*graphState);
            mailbox.graphReplacementApplied();
        } else {
            for (const NodeGraphDelta& delta : mailbox.graphDeltas()) {
                if (!runtime.applyGraphDelta(delta)) {
                    const NodeGraphState* cachedState = mailbox.graphReplacement(true);
                    Q_ASSERT(cachedState);
                    if (cachedState) runtime.replaceGraphState(*cachedState);
                    break;
                }
            }
            mailbox.graphDeltasApplied();
        }

        app::WireframeMode wireframeMode{};
        if (mailbox.takeWireframeMode(wireframeMode, runtimeFresh)) {
            runtime.setWireframeMode(wireframeMode);
        }
        bool gridEnabled = false;
        if (mailbox.takeGridEnabled(gridEnabled, runtimeFresh)) {
            runtime.setGridEnabled(gridEnabled);
        }
        app::BackgroundMode backgroundMode{};
        if (mailbox.takeBackgroundMode(backgroundMode, runtimeFresh)) {
            runtime.setBackgroundMode(backgroundMode);
        }
        bool navigationCubeVisible = true;
        if (mailbox.takeNavigationCubeVisible(navigationCubeVisible, runtimeFresh)) {
            runtime.setNavigationCubeVisible(navigationCubeVisible);
        }
        bool axisLabelsVisible = true;
        if (mailbox.takeAxisLabelsVisible(axisLabelsVisible, runtimeFresh)) {
            runtime.setAxisLabelsVisible(axisLabelsVisible);
        }
        if (mailbox.takeFocusWorldOrigin()) {
            runtime.focusCameraOnWorldOrigin();
        }
        CameraProjectionMode projectionMode{};
        if (mailbox.takeProjectionMode(projectionMode)) {
            runtime.setCameraProjectionMode(projectionMode);
        }
        float cameraFov = Camera::DefaultFov;
        if (mailbox.takeCameraFov(cameraFov)) {
            runtime.setCameraFov(cameraFov);
        }
        float cameraZoomSpeed = Camera::DefaultZoomSpeed;
        if (mailbox.takeCameraZoomSpeed(cameraZoomSpeed)) {
            runtime.setCameraZoomSpeed(cameraZoomSpeed);
        }
        float cameraPanSpeed = Camera::DefaultPanSpeed;
        if (mailbox.takeCameraPanSpeed(cameraPanSpeed)) {
            runtime.setCameraPanSpeed(cameraPanSpeed);
        }

        TimelineController* timeline = runtime.timelineController();
        uint32_t frameCount = 0;
        float fps = 0.0f;
        if (mailbox.takeTimelineRange(frameCount, fps, runtimeFresh)) {
            timeline->setFps(fps);
            timeline->setFrameCount(frameCount);
        }
        if (mailbox.takeTimelineReset()) {
            timeline->reset();
        }
        const int timelineStep = mailbox.takeTimelineStep();
        if (timelineStep != 0) {
            timeline->stepFrames(timelineStep);
        }
        bool timelinePlaying = false;
        if (mailbox.takeTimelinePlaying(timelinePlaying, runtimeFresh)) {
            timeline->setPlaying(timelinePlaying);
        }
        int selectedNode = 0;
        if (mailbox.takeSelection(selectedNode, runtimeFresh)) {
            applySelection(selectedNode);
        }
        float paletteMin = 0.0f, paletteMax = 100.0f;
        if (mailbox.takeHeatPaletteRange(paletteMin, paletteMax, runtimeFresh)) runtime.setHeatPaletteRange(paletteMin, paletteMax);
        int palette = 0;
        if (mailbox.takeHeatPalette(palette, runtimeFresh)) runtime.setHeatPalette(palette);
        ProjectFile::Viewport viewportProjectState;
        if (mailbox.takeAppliedViewportProjectState(viewportProjectState)) {
            runtime.setBackgroundMode(viewportProjectState.backgroundMode);
            runtime.setNavigationCubeVisible(viewportProjectState.navigationCubeVisible);
            runtime.setAxisLabelsVisible(viewportProjectState.axisLabelsVisible);
            if (CameraController* cameraController = runtime.getCameraController()) {
                cameraController->setCameraState(
                    viewportProjectState.lookAt,
                    viewportProjectState.orientation,
                    viewportProjectState.radius,
                    viewportProjectState.fov,
                    viewportProjectState.projectionMode,
                    viewportProjectState.orthographicHeight,
                    viewportProjectState.zoomSpeed,
                    viewportProjectState.panSpeed);
            }
        }
        if (mailbox.takeCurrentViewportProjectState() && viewportItemForSignals) {
            if (const CameraController* cameraController = runtime.getCameraController()) {
                const Camera& camera = cameraController->getCamera();
                ProjectFile::Viewport viewport{};
                viewport.lookAt = camera.getLookAt();
                viewport.orientation = camera.getOrientation();
                viewport.radius = camera.getRadius();
                viewport.fov = camera.getBaseFov();
                viewport.projectionMode = camera.getProjectionMode();
                viewport.orthographicHeight = camera.getOrthographicHeight();
                viewport.zoomSpeed = camera.getZoomSpeed();
                viewport.panSpeed = camera.getPanSpeed();
                const app::RenderSettings& renderSettings = runtime.renderSettings();
                viewport.backgroundMode = renderSettings.backgroundMode;
                viewport.navigationCubeVisible = renderSettings.navigationCubeVisible;
                viewport.axisLabelsVisible = renderSettings.axisLabelsVisible;
                emit viewportItemForSignals->projectViewportStateReady(viewport);
            }
        }
        runtimeFresh = false;
    }

    void render(QRhiCommandBuffer* commandBuffer) override {
        if (!initialized || !commandBuffer || !colorTexture() || !rhi()) {
            return;
        }

        float deltaSeconds = 1.0f / 60.0f;
        if (frameTimer.isValid()) {
            const qint64 elapsedNanoseconds = frameTimer.nsecsElapsed();
            frameTimer.restart();
            deltaSeconds = std::clamp(
                static_cast<float>(elapsedNanoseconds) * 1.0e-9f,
                0.0f,
                0.1f);
        } else {
            frameTimer.start();
        }

        const uint32_t scrubFrame = mailbox.takeTimelineScrub();
        if (scrubFrame != ViewportMailbox::noPendingScrubFrame) {
            runtime.timelineController()->scrubToFrame(scrubFrame);
        }

        commandBuffer->beginExternal();
        const QRhiVulkanCommandBufferNativeHandles* nativeCommandBuffer =
            static_cast<const QRhiVulkanCommandBufferNativeHandles*>(commandBuffer->nativeHandles());
        if (!nativeCommandBuffer || nativeCommandBuffer->commandBuffer == VK_NULL_HANDLE) {
            commandBuffer->endExternal();
            return;
        }

        runtime.tickFrame(
            deltaSeconds,
            nativeCommandBuffer->commandBuffer,
            static_cast<uint32_t>(rhi()->currentFrameSlot()));
        commandBuffer->endExternal();
        NodeGraphNodeId nodeId{};
        std::vector<NodeGraphParamValue> parameters;
        while (runtime.takePendingNodeParameters(nodeId, parameters)) {
            emit notifier.nodeParametersRequested(nodeId, parameters);
        }
        colorTexture()->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!snapshotTimer.isValid() || snapshotTimer.elapsed() >= 16) {
            publishState();
            snapshotTimer.restart();
        }

        update();
    }

private:
    void applySelection(int selectedNode) {
        ModelSelection* selection = runtime.getModelSelection();
        NodeGraphController* controller = runtime.getNodeGraphController();
        if (!selection) return;
        selection->clearSelection();
        if (selectedNode <= 0 || !controller) return;
        std::vector<uint32_t> runtimeModelIds;
        if (!controller->runtimeModelIdsForNode(NodeGraphNodeId{static_cast<uint32_t>(selectedNode)}, runtimeModelIds)) return;
        for (uint32_t id : runtimeModelIds) selection->addSelectedModelID(id);
    }

    void publishState() {
        const RuntimeQuery* query = runtime.runtimeQuery();
        if (!query) return;

        const app::RenderSettings& renderSettings = runtime.renderSettings();
        ViewportUiState viewport{};
        viewport.wireframeMode = renderSettings.wireframeMode;
        viewport.gridEnabled = renderSettings.gridEnabled;
        viewport.backgroundMode = renderSettings.backgroundMode;
        viewport.navigationCubeVisible = renderSettings.navigationCubeVisible;
        viewport.axisLabelsVisible = renderSettings.axisLabelsVisible;
        if (const CameraController* cameraController = runtime.getCameraController()) {
            const Camera& camera = cameraController->getCamera();
            viewport.projectionMode = camera.getProjectionMode();
            viewport.baseFov = camera.getBaseFov();
            viewport.zoomSpeed = camera.getZoomSpeed();
            viewport.panSpeed = camera.getPanSpeed();
        }
        if (viewport != publishedViewport) {
            publishedViewport = viewport;
            emit notifier.viewportStateChanged(viewport);
        }
        const bool paletteVisible = runtime.isHeatPaletteVisible();
        if (paletteVisible != publishedHeatPaletteVisible) {
            publishedHeatPaletteVisible = paletteVisible;
            emit notifier.heatPaletteVisibilityChanged(paletteVisible);
        }

        TimelineUiState timeline{};
        timeline.playing = query->isTimelinePlaying();
        timeline.currentFrame = query->getTimelineCurrentFrame();
        timeline.frameCount = query->getTimelineFrameCount();
        timeline.recordedFrames = query->getSimulationRecordedTimelineFrames();
        timeline.startFrame = query->getTimelineStartDisplayFrame();
        timeline.endFrame = query->getTimelineEndDisplayFrame();
        timeline.currentSeconds = query->getTimelineCurrentSeconds();
        timeline.durationSeconds = query->getTimelineDurationSeconds();
        if (timeline != publishedTimeline) {
            publishedTimeline = timeline;
            emit notifier.timelineStateChanged(timeline);
        }

        const bool simulationActive = query->isSimulationActive();
        const bool simulationPaused = query->isSimulationPaused();
        if (simulationActive != publishedSimulationActive ||
            simulationPaused != publishedSimulationPaused) {
            publishedSimulationActive = simulationActive;
            publishedSimulationPaused = simulationPaused;
            notifier.publishHeatSolveStatus(simulationActive, simulationPaused);
        }
    }

    RuntimeSystems& runtime;
    RuntimeNotifier& notifier;
    ViewportMailbox& mailbox;
    QElapsedTimer frameTimer;
    QElapsedTimer snapshotTimer;
    ViewportUiState publishedViewport{};
    bool publishedHeatPaletteVisible = false;
    TimelineUiState publishedTimeline{};
    ViewportItem* viewportItemForSignals = nullptr;
    bool publishedSimulationActive = false;
    bool publishedSimulationPaused = false;
    bool initialized = false;
    bool runtimeFresh = false;
};

ViewportItem::ViewportItem(QQuickItem* parent)
    : QQuickRhiItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
}

void ViewportItem::bindRuntime(RuntimeSystems& runtime_, RuntimeNotifier& notifier_) {
    Q_ASSERT(!runtime && !notifier);
    runtime = &runtime_;
    notifier = &notifier_;
}

void ViewportItem::requestWorldUnit(int unit) {
    mailbox.requestWorldUnit(unit);
    update();
}

void ViewportItem::requestWireframeMode(app::WireframeMode mode) { mailbox.requestWireframeMode(mode); update(); }
void ViewportItem::requestGridEnabled(bool enabled) { mailbox.requestGridEnabled(enabled); update(); }
void ViewportItem::requestBackgroundMode(app::BackgroundMode mode) { mailbox.requestBackgroundMode(mode); update(); }
void ViewportItem::requestNavigationCubeVisible(bool visible) { mailbox.requestNavigationCubeVisible(visible); update(); }
void ViewportItem::requestAxisLabelsVisible(bool visible) { mailbox.requestAxisLabelsVisible(visible); update(); }
void ViewportItem::requestFocusWorldOrigin() { mailbox.requestFocusWorldOrigin(); update(); }
void ViewportItem::requestProjectionMode(CameraProjectionMode mode) { mailbox.requestProjectionMode(mode); update(); }
void ViewportItem::requestCameraFov(float degrees) { mailbox.requestCameraFov(degrees); update(); }
void ViewportItem::requestCameraZoomSpeed(float speed) { mailbox.requestCameraZoomSpeed(speed); update(); }
void ViewportItem::requestCameraPanSpeed(float speed) { mailbox.requestCameraPanSpeed(speed); update(); }
void ViewportItem::requestTimelinePlaying(bool playing) { mailbox.requestTimelinePlaying(playing); update(); }
void ViewportItem::requestTimelineReset() { mailbox.requestTimelineReset(); update(); }
void ViewportItem::requestTimelineScrub(uint32_t frame) { mailbox.requestTimelineScrub(frame); }
void ViewportItem::requestTimelineStep(int delta) { mailbox.requestTimelineStep(delta); update(); }
void ViewportItem::requestTimelineRange(uint32_t frameCount, float fps) { mailbox.requestTimelineRange(frameCount, fps); update(); }
void ViewportItem::requestSelection(int nodeId) { mailbox.requestSelection(nodeId); update(); }
void ViewportItem::requestHeatPaletteRange(float minimum, float maximum) { mailbox.requestHeatPaletteRange(minimum, maximum); update(); }
void ViewportItem::requestHeatPalette(int palette) { mailbox.requestHeatPalette(palette); update(); }
void ViewportItem::applyViewportProjectState(const ProjectFile::Viewport& viewport) {
    mailbox.applyViewportProjectState(viewport);
    update();
}

void ViewportItem::requestCurrentViewportProjectState() {
    mailbox.requestCurrentViewportProjectState();
    update();
}

void ViewportItem::initializeGraph(const NodeGraphState& graphState) {
    mailbox.replaceGraphState(graphState);
    update();
}

void ViewportItem::queueGraphDelta(const NodeGraphDelta& delta) {
    mailbox.appendGraphDelta(delta);
    update();
}

QQuickRhiItemRenderer* ViewportItem::createRenderer() {
    Q_ASSERT(runtime && notifier);
    return runtime && notifier ? new ViewportRenderer(*runtime, *notifier, mailbox) : nullptr;
}

void ViewportItem::keyPressEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QQuickRhiItem::keyPressEvent(event);
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        state.shiftPressed.store(true, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = true;
    inputEvent.ctrlPressed = event->modifiers().testFlag(Qt::ControlModifier);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::keyReleaseEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QQuickRhiItem::keyReleaseEvent(event);
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        state.shiftPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = false;
    inputEvent.ctrlPressed = event->modifiers().testFlag(Qt::ControlModifier);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::mousePressEvent(QMouseEvent* event) {
    if (!event) {
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    forceActiveFocus();
    updateMousePosition(event->position());
    if (event->button() == Qt::MiddleButton) {
        state.middleButtonPressed.store(true, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MousePress;
    inputEvent.button = static_cast<int>(event->button());
    inputEvent.shiftPressed = event->modifiers().testFlag(Qt::ShiftModifier);
    inputEvent.x = state.mouseX.load(std::memory_order_acquire);
    inputEvent.y = state.mouseY.load(std::memory_order_acquire);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::mouseReleaseEvent(QMouseEvent* event) {
    if (!event) {
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    updateMousePosition(event->position());
    if (event->button() == Qt::MiddleButton) {
        state.middleButtonPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseRelease;
    inputEvent.button = static_cast<int>(event->button());
    inputEvent.x = state.mouseX.load(std::memory_order_acquire);
    inputEvent.y = state.mouseY.load(std::memory_order_acquire);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::mouseMoveEvent(QMouseEvent* event) {
    if (!event) {
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    updateMousePosition(event->position());
    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseMove;
    inputEvent.x = state.mouseX.load(std::memory_order_acquire);
    inputEvent.y = state.mouseY.load(std::memory_order_acquire);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::hoverMoveEvent(QHoverEvent* event) {
    if (!event) {
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    updateMousePosition(event->position());
    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseMove;
    inputEvent.x = state.mouseX.load(std::memory_order_acquire);
    inputEvent.y = state.mouseY.load(std::memory_order_acquire);
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::hoverLeaveEvent(QHoverEvent* event) {
    WindowRuntimeState& state = mailbox.runtimeState();
    state.mouseX.store(-1.0f, std::memory_order_release);
    state.mouseY.store(-1.0f, std::memory_order_release);
    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseMove;
    inputEvent.x = -1.0f;
    inputEvent.y = -1.0f;
    state.pushInputEvent(inputEvent);
    if (event) {
        event->accept();
    }
}

void ViewportItem::wheelEvent(QWheelEvent* event) {
    if (!event) {
        return;
    }

    WindowRuntimeState& state = mailbox.runtimeState();
    updateMousePosition(event->position());
    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Scroll;
    inputEvent.yOffset = static_cast<double>(event->angleDelta().y()) / 120.0;
    state.pushInputEvent(inputEvent);
    event->accept();
}

void ViewportItem::focusOutEvent(QFocusEvent* event) {
    WindowRuntimeState& state = mailbox.runtimeState();
    state.shiftPressed.store(false, std::memory_order_release);
    state.middleButtonPressed.store(false, std::memory_order_release);
    QQuickRhiItem::focusOutEvent(event);
}

void ViewportItem::updateMousePosition(const QPointF& position) {
    WindowRuntimeState& state = mailbox.runtimeState();
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    state.mouseX.store(static_cast<float>(position.x() * dpr), std::memory_order_release);
    state.mouseY.store(static_cast<float>(position.y() * dpr), std::memory_order_release);
}
