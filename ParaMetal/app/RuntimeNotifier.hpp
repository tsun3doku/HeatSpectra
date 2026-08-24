#pragma once

#include "UiRuntimeTypes.hpp"

#include <QMutex>
#include <QObject>
#include <QString>

class RuntimeNotifier final : public QObject {
    Q_OBJECT

public:
    explicit RuntimeNotifier(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool heatSolveActive() const {
        const QMutexLocker lock(&statusMutex);
        return heatSolveActiveState;
    }
    Q_INVOKABLE bool heatSolvePaused() const {
        const QMutexLocker lock(&statusMutex);
        return heatSolvePausedState;
    }
    Q_INVOKABLE QString serialConnectionText() const {
        const QMutexLocker lock(&statusMutex);
        return serialConnectionState;
    }
    Q_INVOKABLE QString serialTemperatureText() const {
        const QMutexLocker lock(&statusMutex);
        return serialTemperatureState;
    }
    Q_INVOKABLE QString serialPollingRateText() const {
        const QMutexLocker lock(&statusMutex);
        return serialPollingRateState;
    }
    Q_INVOKABLE QString heatSolveError() const {
        const QMutexLocker lock(&statusMutex);
        return heatSolveErrorState;
    }

    void publishHeatSolveStatus(bool active, bool paused) {
        {
            const QMutexLocker lock(&statusMutex);
            if (heatSolveActiveState == active && heatSolvePausedState == paused) return;
            heatSolveActiveState = active;
            heatSolvePausedState = paused;
        }
        emit heatSolveStatusChanged(active, paused);
    }

    void publishHeatSolveError(const QString& error) {
        {
            const QMutexLocker lock(&statusMutex);
            if (heatSolveErrorState == error) return;
            heatSolveErrorState = error;
        }
        emit heatSolveErrorChanged(error);
    }

    void publishSerialStatus(const QString& connection, const QString& temperature, const QString& pollingRate) {
        {
            const QMutexLocker lock(&statusMutex);
            if (serialConnectionState == connection &&
                serialTemperatureState == temperature &&
                serialPollingRateState == pollingRate) return;
            serialConnectionState = connection;
            serialTemperatureState = temperature;
            serialPollingRateState = pollingRate;
        }
        emit serialStatusChanged(connection, temperature, pollingRate);
    }

signals:
    void viewportStateChanged(const ViewportUiState& state);
    void heatPaletteVisibilityChanged(bool visible);
    void timelineStateChanged(const TimelineUiState& state);
    void heatSolveStatusChanged(bool active, bool paused);
    void heatSolveErrorChanged(const QString& error);
    void serialStatusChanged(const QString& connection, const QString& temperature, const QString& pollingRate);
    void graphSelectionChanged(NodeGraphNodeId nodeId);
    void nodeParametersRequested(
        NodeGraphNodeId nodeId,
        const std::vector<NodeGraphParamValue>& parameters);

private:
    mutable QMutex statusMutex;
    bool heatSolveActiveState = false;
    bool heatSolvePausedState = false;
    QString serialConnectionState = QStringLiteral("Not used by an active Heat Solve");
    QString serialTemperatureState = QStringLiteral("--");
    QString serialPollingRateState = QStringLiteral("--");
    QString heatSolveErrorState;
};
