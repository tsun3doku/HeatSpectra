#pragma once

#include "UiRuntimeTypes.hpp"

#include <QObject>
#include <cstdint>

class TimelineUiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY stateChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY stateChanged)
    Q_PROPERTY(int recordedFrames READ recordedFrames NOTIFY stateChanged)
    Q_PROPERTY(int startFrame READ startFrame NOTIFY stateChanged)
    Q_PROPERTY(int endFrame READ endFrame NOTIFY stateChanged)
    Q_PROPERTY(double currentSeconds READ currentSeconds NOTIFY stateChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY stateChanged)

public:
    explicit TimelineUiModel(QObject* parent = nullptr) : QObject(parent) {}

    bool playing() const { return state.playing; }
    int currentFrame() const { return static_cast<int>(state.currentFrame); }
    int frameCount() const { return static_cast<int>(state.frameCount); }
    int recordedFrames() const { return static_cast<int>(state.recordedFrames); }
    int startFrame() const { return static_cast<int>(state.startFrame); }
    int endFrame() const { return static_cast<int>(state.endFrame); }
    double currentSeconds() const { return state.currentSeconds; }
    double durationSeconds() const { return state.durationSeconds; }

    Q_INVOKABLE void setPlaying(bool playing);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void scrub(int frame);
    Q_INVOKABLE void step(int delta);

public slots:
    void applyState(const TimelineUiState& updated);

signals:
    void stateChanged();
    void playingRequested(bool playing);
    void resetRequested();
    void scrubRequested(uint32_t frame);
    void stepRequested(int delta);

private:
    TimelineUiState state{};
};
