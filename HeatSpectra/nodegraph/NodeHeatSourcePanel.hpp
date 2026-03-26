#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QString;

class NodeHeatSourcePanel final : public QWidget {
public:
    explicit NodeHeatSourcePanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    void applySettings();
    void refreshFromNode();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};

    QLabel* temperatureLabel = nullptr;
    QDoubleSpinBox* temperatureSpin = nullptr;
    QPushButton* applyButton = nullptr;

    std::function<void(const QString&)> statusSink;
};
