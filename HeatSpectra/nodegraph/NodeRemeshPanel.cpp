#include "NodeRemeshPanel.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

NodeRemeshPanel::NodeRemeshPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* iterationsRow = new QHBoxLayout();
    iterationsRow->addWidget(new QLabel("Iterations:", this));
    iterationsSpinBox = new QSpinBox(this);
    iterationsSpinBox->setMinimum(1);
    iterationsSpinBox->setMaximum(1000);
    iterationsSpinBox->setValue(1);
    iterationsRow->addWidget(iterationsSpinBox, 1);
    layout->addLayout(iterationsRow);

    QHBoxLayout* minAngleRow = new QHBoxLayout();
    minAngleRow->addWidget(new QLabel("Min Angle:", this));
    minAngleSpinBox = new QDoubleSpinBox(this);
    minAngleSpinBox->setMinimum(0.0);
    minAngleSpinBox->setMaximum(60.0);
    minAngleSpinBox->setSingleStep(1.0);
    minAngleSpinBox->setValue(30.0);
    minAngleRow->addWidget(minAngleSpinBox, 1);
    layout->addLayout(minAngleRow);

    QHBoxLayout* maxEdgeRow = new QHBoxLayout();
    maxEdgeRow->addWidget(new QLabel("Max Edge Length:", this));
    maxEdgeLengthSpinBox = new QDoubleSpinBox(this);
    maxEdgeLengthSpinBox->setMinimum(0.001);
    maxEdgeLengthSpinBox->setMaximum(10.0);
    maxEdgeLengthSpinBox->setDecimals(4);
    maxEdgeLengthSpinBox->setSingleStep(0.01);
    maxEdgeLengthSpinBox->setValue(0.1);
    maxEdgeRow->addWidget(maxEdgeLengthSpinBox, 1);
    layout->addLayout(maxEdgeRow);

    QHBoxLayout* stepRow = new QHBoxLayout();
    stepRow->addWidget(new QLabel("Step Size:", this));
    stepSizeSpinBox = new QDoubleSpinBox(this);
    stepSizeSpinBox->setMinimum(0.01);
    stepSizeSpinBox->setMaximum(1.0);
    stepSizeSpinBox->setSingleStep(0.05);
    stepSizeSpinBox->setDecimals(2);
    stepSizeSpinBox->setValue(0.25);
    stepRow->addWidget(stepSizeSpinBox, 1);
    layout->addLayout(stepRow);

    QHBoxLayout* actionRow = new QHBoxLayout();
    applyButton = new QPushButton("Apply", this);
    runButton = new QPushButton("Run Remesh", this);
    actionRow->addWidget(applyButton);
    actionRow->addWidget(runButton);
    layout->addLayout(actionRow);

    layout->addStretch();

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
    });
    connect(runButton, &QPushButton::clicked, this, [this]() {
        executeRemesh();
    });
}

void NodeRemeshPanel::bind(NodeGraphBridge* bridge) {
    nodeGraphBridge = bridge;
}

void NodeRemeshPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    iterationsSpinBox->setValue(NodePanelUtils::readIntParam(node, nodegraphparams::remesh::Iterations, 1));
    minAngleSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MinAngleDegrees, 30.0));
    maxEdgeLengthSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MaxEdgeLength, 0.1));
    stepSizeSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::StepSize, 0.25));
}

void NodeRemeshPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
}

void NodeRemeshPanel::applySettings() {
    if (!nodeGraphBridge) {
        setStatus("Cannot apply settings for this node");
        return;
    }

    if (!NodePanelUtils::writeIntParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::Iterations, iterationsSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MinAngleDegrees, minAngleSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MaxEdgeLength, maxEdgeLengthSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::StepSize, stepSizeSpinBox->value())) {
        setStatus("Failed to update remesh settings");
        return;
    }

    setStatus("Remesh settings updated");
}

void NodeRemeshPanel::executeRemesh() {
    if (!nodeGraphBridge) {
        setStatus("Cannot run remesh for this node");
        return;
    }

    applySettings();
    if (!NodePanelUtils::writeBoolParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::RunRequested, true)) {
        setStatus("Failed to request remesh");
        return;
    }

    setStatus("Remesh requested through node graph");
}

void NodeRemeshPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
