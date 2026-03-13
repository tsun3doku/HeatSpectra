#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QString;

class NodeContactPairPanel final : public QWidget {
public:
    explicit NodeContactPairPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    void applySettings();
    void requestCompute();
    void refreshFromNode();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};

    QLabel* bodyALabel = nullptr;
    QLabel* bodyBLabel = nullptr;
    QDoubleSpinBox* minNormalDotSpin = nullptr;
    QDoubleSpinBox* contactRadiusSpin = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* computeButton = nullptr;

    std::function<void(const QString&)> statusSink;
};
