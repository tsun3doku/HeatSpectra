#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QCheckBox;
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
    bool writeParameters();
    void refreshFromNode();
    void onParametersEdited();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};
    bool syncingFromNode = false;

    QLabel* emitterLabel = nullptr;
    QLabel* receiverLabel = nullptr;
    QDoubleSpinBox* minNormalDotSpin = nullptr;
    QDoubleSpinBox* contactRadiusSpin = nullptr;
    QCheckBox* showContactLinesCheckBox = nullptr;

    std::function<void(const QString&)> statusSink;
};
