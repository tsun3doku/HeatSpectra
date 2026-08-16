#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

#include "AppTypes.hpp"
#include "UiRuntimeTypes.hpp"
#include "nodegraph/NodeGraphState.hpp"
#include "project/ProjectFile.hpp"
#include "render/WindowRuntimeState.hpp"

class ViewportMailbox final {
public:
    static constexpr uint32_t noPendingScrubFrame = std::numeric_limits<uint32_t>::max();

    ViewportMailbox() = default;
    ViewportMailbox(const ViewportMailbox&) = delete;
    ViewportMailbox& operator=(const ViewportMailbox&) = delete;

    WindowRuntimeState& runtimeState() { return state; }

    void requestWireframeMode(app::WireframeMode mode);
    void requestGridEnabled(bool enabled);
    void requestBackgroundMode(app::BackgroundMode mode);
    void requestNavigationCubeVisible(bool visible);
    void requestAxisLabelsVisible(bool visible);
    void requestFocusWorldOrigin();
    void requestProjectionMode(CameraProjectionMode mode);
    void requestCameraFov(float degrees);
    void requestCameraZoomSpeed(float speed);
    void requestCameraPanSpeed(float speed);
    void requestTimelinePlaying(bool playing);
    void requestTimelineReset();
    void requestTimelineScrub(uint32_t frame);
    void requestTimelineStep(int delta);
    void requestTimelineRange(uint32_t frameCount, float fps);
    void requestSelection(int nodeId);
    void requestHeatPaletteRange(float minimum, float maximum);
    void requestHeatPalette(int palette);
    void requestWorldUnit(int unit);
    void replaceGraphState(const NodeGraphState& graphState);
    void applyViewportProjectState(const ProjectFile::Viewport& viewport);
    void requestCurrentViewportProjectState();
    void appendGraphDelta(const NodeGraphDelta& delta);

    bool takeWireframeMode(app::WireframeMode& mode, bool force = false);
    bool takeGridEnabled(bool& enabled, bool force = false);
    bool takeBackgroundMode(app::BackgroundMode& mode, bool force = false);
    bool takeNavigationCubeVisible(bool& visible, bool force = false);
    bool takeAxisLabelsVisible(bool& visible, bool force = false);
    bool takeFocusWorldOrigin();
    bool takeProjectionMode(CameraProjectionMode& mode);
    bool takeCameraFov(float& degrees);
    bool takeCameraZoomSpeed(float& speed);
    bool takeCameraPanSpeed(float& speed);
    bool takeTimelinePlaying(bool& playing, bool force = false);
    bool takeTimelineReset();
    int takeTimelineStep();
    uint32_t takeTimelineScrub();
    bool takeTimelineRange(uint32_t& frameCount, float& fps, bool force = false);
    bool takeSelection(int& nodeId, bool force = false);
    bool takeHeatPaletteRange(float& minimum, float& maximum, bool force = false);
    bool takeHeatPalette(int& palette, bool force = false);
    bool takeWorldUnit(int& unit, bool force = false);
    bool takeAppliedViewportProjectState(ProjectFile::Viewport& viewport);
    bool takeCurrentViewportProjectState();

    const NodeGraphState* graphReplacement(bool force = false) const;
    void graphReplacementApplied();
    const std::vector<NodeGraphDelta>& graphDeltas() const { return pendingGraphDeltas; }
    void graphDeltasApplied() { pendingGraphDeltas.clear(); }

private:
    WindowRuntimeState state;

    app::WireframeMode requestedWireframeMode = app::WireframeMode::Off;
    bool wireframeDirty = false;
    bool requestedGridEnabled = app::RenderSettings{}.gridEnabled;
    bool gridDirty = false;
    app::BackgroundMode requestedBackgroundMode = app::RenderSettings{}.backgroundMode;
    bool backgroundModeDirty = false;
    bool requestedNavigationCubeVisible = app::RenderSettings{}.navigationCubeVisible;
    bool navigationCubeVisibleDirty = false;
    bool requestedAxisLabelsVisible = app::RenderSettings{}.axisLabelsVisible;
    bool axisLabelsVisibleDirty = false;
    bool focusWorldOriginPending = false;
    CameraProjectionMode requestedProjectionMode = CameraProjectionMode::Perspective;
    bool projectionModeDirty = false;
    float requestedCameraFov = Camera::DefaultFov;
    bool cameraFovDirty = false;
    float requestedCameraZoomSpeed = Camera::DefaultZoomSpeed;
    bool cameraZoomSpeedDirty = false;
    float requestedCameraPanSpeed = Camera::DefaultPanSpeed;
    bool cameraPanSpeedDirty = false;

    bool requestedTimelinePlaying = false;
    bool timelinePlayingDirty = false;
    bool timelineResetPending = false;
    int requestedTimelineStep = 0;
    std::atomic<uint32_t> pendingScrubFrame{noPendingScrubFrame};
    uint32_t requestedTimelineFrameCount = 251;
    float requestedTimelineFps = 60.0f;
    bool timelineRangeDirty = false;

    int requestedSelectedNode = 0;
    bool selectionDirty = false;

    float requestedHeatPaletteMin = 0.0f;
    float requestedHeatPaletteMax = 100.0f;
    bool heatPaletteRangeDirty = false;
    int requestedHeatPalette = 0;
    bool heatPaletteDirty = false;
    int requestedWorldUnit = static_cast<int>(units::defaultLengthUnit());
    bool worldUnitDirty = false;

    ProjectFile::Viewport appliedViewportProjectState;
    bool viewportProjectStateDirty = false;
    bool currentViewportProjectStateRequested = false;

    NodeGraphState cachedGraphState;
    std::vector<NodeGraphDelta> pendingGraphDeltas;
    bool graphStateInitialized = false;
    bool graphStateDirty = false;
};
