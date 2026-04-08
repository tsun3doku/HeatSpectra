#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

#include <functional>

class NodeGraphBridge;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QString;

class NodeModelPanel final : public QWidget {
public:
    explicit NodeModelPanel(QWidget* parent = nullptr);

    void bind(NodeGraphBridge* nodeGraphBridge);
    void setNode(NodeGraphNodeId nodeId);
    void setStatusSink(std::function<void(const QString&)> statusSink);

private:
    bool writeParameters();
    void browseModelFile();
    void applySettings();
    void setStatus(const QString& text) const;

    NodeGraphBridge* nodeGraphBridge = nullptr;
    NodeGraphNodeId currentNodeId{};
    bool syncingFromNode = false;

    QLineEdit* pathLineEdit = nullptr;
    QPushButton* browseButton = nullptr;
    QCheckBox* wireframeCheckBox = nullptr;

    std::function<void(const QString&)> statusSink;
};
