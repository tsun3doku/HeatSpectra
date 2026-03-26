#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QLabel;
class QString;

class NodeContactPanel final : public QWidget {
public:
    explicit NodeContactPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    void refreshFromNode();
    void setStatus(const QString& text) const;
    void writeMinNormalDot(double value);
    void writeContactRadius(double value);

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};

    QLabel* emitterLabel = nullptr;
    QLabel* receiverLabel = nullptr;
    QDoubleSpinBox* minNormalDotSpin = nullptr;
    QDoubleSpinBox* contactRadiusSpin = nullptr;

    std::function<void(const QString&)> statusSink;
};
