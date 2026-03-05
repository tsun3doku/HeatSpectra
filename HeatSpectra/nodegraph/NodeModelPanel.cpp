#include "NodeModelPanel.hpp"

#include "NodeGraphBridge.hpp"
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

    applyButton = new QPushButton("Apply Selected Model", this);
    layout->addWidget(applyButton);

    QLabel* hintLabel = new QLabel(
        "Model nodes are independent. Their downstream graph wiring "
        "determines how runtime consumes this mesh.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    layout->addStretch();

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        browseModelFile();
    });
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
    });
}

void NodeModelPanel::bind(NodeGraphBridge* bridge) {
    nodeGraphBridge = bridge;
}

void NodeModelPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    pathLineEdit->setText(QString::fromStdString(
        NodePanelUtils::readStringParam(node, nodegraphparams::model::Path)));
}

void NodeModelPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
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
    if (!nodeGraphBridge || !pathLineEdit) {
        setStatus("Cannot apply model settings for this node.");
        return;
    }

    const std::string modelPath = pathLineEdit->text().trimmed().toStdString();
    if (modelPath.empty()) {
        setStatus("Model path cannot be empty.");
        return;
    }

    if (!NodePanelUtils::writeStringParam(nodeGraphBridge, currentNodeId, nodegraphparams::model::Path, modelPath) ||
        !NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::model::ApplyRequested, true)) {
        setStatus("Failed to update model settings.");
        return;
    }

    setStatus("Model path applied.");
}

void NodeModelPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
