#include "ViewportUiModel.hpp"

void ViewportUiModel::setWireframeMode(int mode) {
    Q_ASSERT(mode >= static_cast<int>(app::WireframeMode::Off));
    Q_ASSERT(mode <= static_cast<int>(app::WireframeMode::Shaded));
    if (mode < static_cast<int>(app::WireframeMode::Off) ||
        mode > static_cast<int>(app::WireframeMode::Shaded)) return;
    emit wireframeModeRequested(static_cast<app::WireframeMode>(mode));
}

void ViewportUiModel::setGridEnabled(bool enabled) {
    emit gridEnabledRequested(enabled);
}

void ViewportUiModel::setBackgroundMode(int mode) {
    if (mode < static_cast<int>(app::BackgroundMode::Image) ||
        mode > static_cast<int>(app::BackgroundMode::SolidColor)) return;
    emit backgroundModeRequested(static_cast<app::BackgroundMode>(mode));
}

void ViewportUiModel::setNavigationCubeVisible(bool visible) {
    emit navigationCubeVisibleRequested(visible);
}

void ViewportUiModel::setAxisLabelsVisible(bool visible) {
    emit axisLabelsVisibleRequested(visible);
}

void ViewportUiModel::focusWorldOrigin() {
    emit focusWorldOriginRequested();
}

void ViewportUiModel::setProjectionMode(int mode) {
    if (mode < static_cast<int>(CameraProjectionMode::Perspective) ||
        mode > static_cast<int>(CameraProjectionMode::Orthographic)) return;
    emit projectionModeRequested(static_cast<CameraProjectionMode>(mode));
}

void ViewportUiModel::setCameraFov(float degrees) {
    emit cameraFovRequested(degrees);
}

void ViewportUiModel::setCameraZoomSpeed(float speed) {
    emit cameraZoomSpeedRequested(speed);
}

void ViewportUiModel::setCameraPanSpeed(float speed) {
    emit cameraPanSpeedRequested(speed);
}

void ViewportUiModel::applyState(const ViewportUiState& updated) {
    if (state == updated) return;
    state = updated;
    emit stateChanged();
}
