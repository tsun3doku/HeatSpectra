#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QString;

class NodeRemeshPanel final : public QWidget {
public:
    explicit NodeRemeshPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    void applySettings();
    void executeRemesh();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};

    QSpinBox* iterationsSpinBox = nullptr;
    QDoubleSpinBox* minAngleSpinBox = nullptr;
    QDoubleSpinBox* maxEdgeLengthSpinBox = nullptr;
    QDoubleSpinBox* stepSizeSpinBox = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* runButton = nullptr;

    std::function<void(const QString&)> statusSink;
};
