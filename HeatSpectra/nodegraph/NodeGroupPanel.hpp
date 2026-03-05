#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QString;

class NodeGroupPanel final : public QWidget {
public:
    explicit NodeGroupPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void refreshSourceOptions();
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    void applySettings();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};

    QCheckBox* enabledCheckBox = nullptr;
    QComboBox* sourceTypeComboBox = nullptr;
    QComboBox* sourceNameComboBox = nullptr;
    QLineEdit* targetNameLineEdit = nullptr;
    QPushButton* applyButton = nullptr;

    std::function<void(const QString&)> statusSink;
};
