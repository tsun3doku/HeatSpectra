#include "NodeModelPanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeModelParams.hpp"
#include "NodeGraphWidgetStyle.hpp"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

NodeModelPanel::NodeModelPanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    QLabel* pathLabel = new QLabel("Model File:", this);
    layout->addWidget(pathLabel);

    QHBoxLayout* pathRow = new QHBoxLayout();
    pathLineEdit = new QLineEdit(this);
    nodegraphwidgets::styleLineEdit(pathLineEdit);
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

void NodeModelPanel::refreshFromNode() {
    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    const ModelNodeParams params = readModelNodeParams(node);
    setSyncing(true);
    pathLineEdit->setText(QString::fromStdString(params.path));
    setSyncing(false);
}

bool NodeModelPanel::writeParameters() {
    if (!canEdit()) {
        setStatus("Cannot apply model settings for this node.");
        return false;
    }

    const std::string modelPath = pathLineEdit->text().trimmed().toStdString();
    if (modelPath.empty()) {
        setStatus("Model path cannot be empty.");
        return false;
    }

    NodeGraphEditor editor(bridge());
    const ModelNodeParams params{ modelPath };
    if (!writeModelNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update model settings.");
        return false;
    }

    return true;
}

void NodeModelPanel::browseModelFile() {
    if (!bridge()) {
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
    if (isSyncing()) {
        return;
    }

    if (!writeParameters()) {
        return;
    }

    setStatus("Model settings updated.");
}
