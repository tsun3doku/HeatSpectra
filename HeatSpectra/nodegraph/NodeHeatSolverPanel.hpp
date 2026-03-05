#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class RuntimeQuery;
class QComboBox;
class QLabel;
class QPushButton;
class QString;
class QTableWidget;
class QTimer;

class NodeHeatSolverPanel final : public QWidget {
public:
    explicit NodeHeatSolverPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge, const RuntimeQuery* runtimeQuery);
    void setNode(NodeGraphNodeId nodeId);
    void refreshBindingGroupOptions();
    void updateHeatStatus();
    void setStatusSink(std::function<void(const QString&)> statusSink);

    void startStatusTimer();
    void stopStatusTimer();

private:
    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    void applyMaterialBindings();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    const RuntimeQuery* runtimeQuery = nullptr;
    NodeGraphNodeId currentNodeId{};

    QLabel* heatStatusValueLabel = nullptr;
    QPushButton* heatToggleButton = nullptr;
    QPushButton* heatPauseButton = nullptr;
    QPushButton* heatResetButton = nullptr;
    QComboBox* heatBindingGroupComboBox = nullptr;
    QComboBox* heatBindingPresetComboBox = nullptr;
    QPushButton* heatBindingAddButton = nullptr;
    QPushButton* heatBindingRemoveButton = nullptr;
    QPushButton* heatBindingApplyButton = nullptr;
    QTableWidget* heatBindingsTable = nullptr;
    QTimer* heatStatusTimer = nullptr;

    std::function<void(const QString&)> statusSink;
};
