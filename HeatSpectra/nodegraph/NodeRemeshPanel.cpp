#include "NodeRemeshPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"
#include "domain/RemeshParams.hpp"

#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

NodeRemeshPanel::NodeRemeshPanel(QWidget* parent)
    : QWidget(parent) {
    const RemeshParams defaults{};

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* iterationsRow = new QHBoxLayout();
    iterationsRow->addWidget(new QLabel("Iterations:", this));
    iterationsSpinBox = new QSpinBox(this);
    iterationsSpinBox->setMinimum(1);
    iterationsSpinBox->setMaximum(1000);
    iterationsSpinBox->setValue(defaults.iterations);
    iterationsRow->addWidget(iterationsSpinBox, 1);
    layout->addLayout(iterationsRow);

    QHBoxLayout* minAngleRow = new QHBoxLayout();
    minAngleRow->addWidget(new QLabel("Min Angle:", this));
    minAngleSpinBox = new QDoubleSpinBox(this);
    minAngleSpinBox->setMinimum(0.0);
    minAngleSpinBox->setMaximum(60.0);
    minAngleSpinBox->setSingleStep(1.0);
    minAngleSpinBox->setValue(defaults.minAngleDegrees);
    minAngleRow->addWidget(minAngleSpinBox, 1);
    layout->addLayout(minAngleRow);

    QHBoxLayout* maxEdgeRow = new QHBoxLayout();
    maxEdgeRow->addWidget(new QLabel("Max Edge Length:", this));
    maxEdgeLengthSpinBox = new QDoubleSpinBox(this);
    maxEdgeLengthSpinBox->setMinimum(0.001);
    maxEdgeLengthSpinBox->setMaximum(10.0);
    maxEdgeLengthSpinBox->setDecimals(4);
    maxEdgeLengthSpinBox->setSingleStep(0.01);
    maxEdgeLengthSpinBox->setValue(defaults.maxEdgeLength);
    maxEdgeRow->addWidget(maxEdgeLengthSpinBox, 1);
    layout->addLayout(maxEdgeRow);

    QHBoxLayout* stepRow = new QHBoxLayout();
    stepRow->addWidget(new QLabel("Step Size:", this));
    stepSizeSpinBox = new QDoubleSpinBox(this);
    stepSizeSpinBox->setMinimum(0.01);
    stepSizeSpinBox->setMaximum(1.0);
    stepSizeSpinBox->setSingleStep(0.05);
    stepSizeSpinBox->setDecimals(2);
    stepSizeSpinBox->setValue(defaults.stepSize);
    stepRow->addWidget(stepSizeSpinBox, 1);
    layout->addLayout(stepRow);

    QLabel* hintLabel = new QLabel(
        "Remesh runs automatically when the input mesh or these parameters change.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    layout->addStretch();

    connect(iterationsSpinBox, &QSpinBox::valueChanged, this, [this](int) {
        onParametersEdited();
    });
    connect(minAngleSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
    });
    connect(maxEdgeLengthSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
    });
    connect(stepSizeSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
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

    const RemeshParams defaults{};

    syncingFromNode = true;
    const QSignalBlocker iterationsBlock(iterationsSpinBox);
    const QSignalBlocker minAngleBlock(minAngleSpinBox);
    const QSignalBlocker maxEdgeBlock(maxEdgeLengthSpinBox);
    const QSignalBlocker stepBlock(stepSizeSpinBox);
    iterationsSpinBox->setValue(NodePanelUtils::readIntParam(node, nodegraphparams::remesh::Iterations, defaults.iterations));
    minAngleSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MinAngleDegrees, defaults.minAngleDegrees));
    maxEdgeLengthSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::MaxEdgeLength, defaults.maxEdgeLength));
    stepSizeSpinBox->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::remesh::StepSize, defaults.stepSize));
    syncingFromNode = false;
}

void NodeRemeshPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
}

bool NodeRemeshPanel::writeParameters() {
    if (!nodeGraphBridge) {
        setStatus("Cannot update settings for this node");
        return false;
    }

    if (!NodePanelUtils::writeIntParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::Iterations, iterationsSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MinAngleDegrees, minAngleSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::MaxEdgeLength, maxEdgeLengthSpinBox->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::remesh::StepSize, stepSizeSpinBox->value())) {
        setStatus("Failed to update remesh settings");
        return false;
    }

    return true;
}

void NodeRemeshPanel::onParametersEdited() {
    if (syncingFromNode) {
        return;
    }

    if (!currentNodeId.isValid()) {
        setStatus("Cannot update remesh settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Remesh settings applied");
    }
}

void NodeRemeshPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
