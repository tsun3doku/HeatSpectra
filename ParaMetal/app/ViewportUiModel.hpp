#pragma once

#include "UiRuntimeTypes.hpp"

#include <QObject>

class ViewportUiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int wireframeMode READ wireframeMode NOTIFY stateChanged)
    Q_PROPERTY(bool gridEnabled READ gridEnabled NOTIFY stateChanged)

public:
    explicit ViewportUiModel(QObject* parent = nullptr) : QObject(parent) {}

    int wireframeMode() const { return static_cast<int>(state.wireframeMode); }
    bool gridEnabled() const { return state.gridEnabled; }

    Q_INVOKABLE void setWireframeMode(int mode);
    Q_INVOKABLE void setGridEnabled(bool enabled);

public slots:
    void applyState(const ViewportUiState& updated);

signals:
    void stateChanged();
    void wireframeModeRequested(app::WireframeMode mode);
    void gridEnabledRequested(bool enabled);

private:
    ViewportUiState state{};
};
