#pragma once

#include "UiRuntimeTypes.hpp"

#include <QObject>

class ViewportUiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int wireframeMode READ wireframeMode NOTIFY stateChanged)
    Q_PROPERTY(bool gridEnabled READ gridEnabled NOTIFY stateChanged)
    Q_PROPERTY(int backgroundMode READ backgroundMode NOTIFY stateChanged)
    Q_PROPERTY(bool navigationCubeVisible READ navigationCubeVisible NOTIFY stateChanged)
    Q_PROPERTY(bool axisLabelsVisible READ axisLabelsVisible NOTIFY stateChanged)
    Q_PROPERTY(int projectionMode READ projectionMode NOTIFY stateChanged)
    Q_PROPERTY(float cameraFov READ cameraFov NOTIFY stateChanged)
    Q_PROPERTY(float cameraZoomSpeed READ cameraZoomSpeed NOTIFY stateChanged)
    Q_PROPERTY(float cameraPanSpeed READ cameraPanSpeed NOTIFY stateChanged)

public:
    explicit ViewportUiModel(QObject* parent = nullptr) : QObject(parent) {}

    int wireframeMode() const { return static_cast<int>(state.wireframeMode); }
    bool gridEnabled() const { return state.gridEnabled; }
    int backgroundMode() const { return static_cast<int>(state.backgroundMode); }
    bool navigationCubeVisible() const { return state.navigationCubeVisible; }
    bool axisLabelsVisible() const { return state.axisLabelsVisible; }
    int projectionMode() const { return static_cast<int>(state.projectionMode); }
    float cameraFov() const { return state.baseFov; }
    float cameraZoomSpeed() const { return state.zoomSpeed; }
    float cameraPanSpeed() const { return state.panSpeed; }

    Q_INVOKABLE void setWireframeMode(int mode);
    Q_INVOKABLE void setGridEnabled(bool enabled);
    Q_INVOKABLE void setBackgroundMode(int mode);
    Q_INVOKABLE void setNavigationCubeVisible(bool visible);
    Q_INVOKABLE void setAxisLabelsVisible(bool visible);
    Q_INVOKABLE void focusWorldOrigin();
    Q_INVOKABLE void setProjectionMode(int mode);
    Q_INVOKABLE void setCameraFov(float degrees);
    Q_INVOKABLE void setCameraZoomSpeed(float speed);
    Q_INVOKABLE void setCameraPanSpeed(float speed);

public slots:
    void applyState(const ViewportUiState& updated);

signals:
    void stateChanged();
    void wireframeModeRequested(app::WireframeMode mode);
    void gridEnabledRequested(bool enabled);
    void backgroundModeRequested(app::BackgroundMode mode);
    void navigationCubeVisibleRequested(bool visible);
    void axisLabelsVisibleRequested(bool visible);
    void focusWorldOriginRequested();
    void projectionModeRequested(CameraProjectionMode mode);
    void cameraFovRequested(float degrees);
    void cameraZoomSpeedRequested(float speed);
    void cameraPanSpeedRequested(float speed);

private:
    ViewportUiState state{};
};
