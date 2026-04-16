#include "NodeModelPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"
#include "NodeModelParams.hpp"
#include "NodePanelUtils.hpp"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

NodeModelPanel::NodeModelPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel* pathLabel = new QLabel("Model File:", this);
    layout->addWidget(pathLabel);

    QHBoxLayout* pathRow = new QHBoxLayout();
    pathLineEdit = new QLineEdit(this);
    pathLineEdit->setReadOnly(true);
    pathLineEdit->setPlaceholderText("models/teapot.obj");
    pathRow->addWidget(pathLineEdit, 1);

    browseButton = new QPushButton("Browse...", this);
    pathRow->addWidget(browseButton);
    layout->addLayout(pathRow);

    QLabel* hintLabel = new QLabel(
        "Model nodes author geometry only. Their downstream graph wiring "
        "and runtime projection determine how sinks consume this mesh.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    layout->addStretch();

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        browseModelFile();
    });
}

void NodeModelPanel::bind(NodeGraphBridge* bridge) {
    nodeGraphBridge = bridge;
}

void NodeModelPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    NodeGraphNode node{};
    if (!NodePanelUtils::loadNode(nodeGraphBridge, currentNodeId, node)) {
        return;
    }

    const ModelNodeParams params = readModelNodeParams(node);
    syncingFromNode = true;
    pathLineEdit->setText(QString::fromStdString(params.path));
    syncingFromNode = false;
}

void NodeModelPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
}

bool NodeModelPanel::writeParameters() {
    if (!nodeGraphBridge || !currentNodeId.isValid() || !pathLineEdit) {
        setStatus("Cannot apply model settings for this node.");
        return false;
    }

    const std::string modelPath = pathLineEdit->text().trimmed().toStdString();
    if (modelPath.empty()) {
        setStatus("Model path cannot be empty.");
        return false;
    }

    NodeGraphEditor editor(nodeGraphBridge);
    const ModelNodeParams params{ modelPath };
    if (!writeModelNodeParams(editor, currentNodeId, params)) {
        setStatus("Failed to update model settings.");
        return false;
    }

    return true;
}

void NodeModelPanel::browseModelFile() {
    if (!nodeGraphBridge || !pathLineEdit) {
        setStatus("Cannot browse model file for this node.");
        return;
    }

    QString initialPath = pathLineEdit->text().trimmed();
    if (initialPath.isEmpty()) {
        initialPath = QDir::currentPath();
    }

    const QString selectedFilePath = QFileDialog::getOpenFileName(
        this,
        "Select Model File",
        initialPath,
        "OBJ Files (*.obj);;All Files (*.*)");
    if (selectedFilePath.isEmpty()) {
        return;
    }

    const QString normalizedPath = QDir::fromNativeSeparators(selectedFilePath);
    pathLineEdit->setText(normalizedPath);
    applySettings();
}

void NodeModelPanel::applySettings() {
    if (syncingFromNode) {
        return;
    }

    if (!writeParameters()) {
        return;
    }

    setStatus("Model settings updated.");
}

void NodeModelPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
