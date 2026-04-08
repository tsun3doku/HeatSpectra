#include "NodeModelPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

#include <QCheckBox>
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

    wireframeCheckBox = new QCheckBox("Wireframe View", this);
    layout->addWidget(wireframeCheckBox);

    layout->addStretch();

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        browseModelFile();
    });
    connect(wireframeCheckBox, &QCheckBox::toggled, this, [this](bool) {
        applySettings();
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

    pathLineEdit->setText(QString::fromStdString(
        NodePanelUtils::readStringParam(node, nodegraphparams::model::Path)));
    wireframeCheckBox->setChecked(
        NodePanelUtils::readBoolParam(node, nodegraphparams::model::ShowWireframe, false));
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

    if (!NodePanelUtils::writeStringParam(nodeGraphBridge, currentNodeId, nodegraphparams::model::Path, modelPath)) {
        setStatus("Failed to update model settings.");
        return;
    }

    if (!NodePanelUtils::writeBoolParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::model::ShowWireframe,
            wireframeCheckBox && wireframeCheckBox->isChecked())) {
        setStatus("Failed to update model wireframe view.");
        return;
    }

    setStatus("Model settings updated.");
}

void NodeModelPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
