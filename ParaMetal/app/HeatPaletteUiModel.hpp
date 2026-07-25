#pragma once

#include "UiRuntimeTypes.hpp"

#include <QObject>

class HeatPaletteUiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY stateChanged)
    Q_PROPERTY(int paletteId READ paletteId NOTIFY stateChanged)
    Q_PROPERTY(int units READ units NOTIFY stateChanged)
    Q_PROPERTY(double minimumC READ minimumC NOTIFY stateChanged)
    Q_PROPERTY(double maximumC READ maximumC NOTIFY stateChanged)
    Q_PROPERTY(double x READ x NOTIFY stateChanged)
    Q_PROPERTY(double y READ y NOTIFY stateChanged)

public:
    explicit HeatPaletteUiModel(QObject* parent = nullptr) : QObject(parent) {}

    bool visible() const { return state.visible; }
    int paletteId() const { return state.paletteId; }
    int units() const { return state.units; }
    double minimumC() const { return state.minimumC; }
    double maximumC() const { return state.maximumC; }
    double x() const { return state.x; }
    double y() const { return state.y; }

    Q_INVOKABLE void setPaletteId(int value);
    Q_INVOKABLE void setUnits(int value);
    Q_INVOKABLE void setRange(double minimumC, double maximumC);
    Q_INVOKABLE void setPosition(double x, double y);

public slots:
    void applyVisibility(bool visible);

signals:
    void stateChanged();
    void paletteRequested(int palette);
    void rangeRequested(float minimum, float maximum);

private:
    HeatPaletteUiState state{};
};
