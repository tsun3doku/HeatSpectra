#include "NodeContactPairPanel.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

NodeContactPairPanel::NodeContactPairPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* bodyARow = new QHBoxLayout();
    bodyARow->addWidget(new QLabel("Emitter:", this));
    bodyALabel = new QLabel("(none)", this);
    bodyARow->addWidget(bodyALabel, 1);
    layout->addLayout(bodyARow);

    QHBoxLayout* bodyBRow = new QHBoxLayout();
    bodyBRow->addWidget(new QLabel("Receiver:", this));
    bodyBLabel = new QLabel("(none)", this);
    bodyBRow->addWidget(bodyBLabel, 1);
    layout->addLayout(bodyBRow);

    QHBoxLayout* minNormalDotRow = new QHBoxLayout();
    minNormalDotRow->addWidget(new QLabel("Min Normal Dot:", this));
    minNormalDotSpin = new QDoubleSpinBox(this);
    minNormalDotSpin->setRange(-1.0, 1.0);
    minNormalDotSpin->setSingleStep(0.05);
    minNormalDotSpin->setDecimals(3);
    minNormalDotSpin->setValue(-0.65);
    minNormalDotRow->addWidget(minNormalDotSpin, 1);
    layout->addLayout(minNormalDotRow);

    QHBoxLayout* contactRadiusRow = new QHBoxLayout();
    contactRadiusRow->addWidget(new QLabel("Contact Radius:", this));
    contactRadiusSpin = new QDoubleSpinBox(this);
    contactRadiusSpin->setRange(0.0001, 10.0);
    contactRadiusSpin->setSingleStep(0.005);
    contactRadiusSpin->setDecimals(4);
    contactRadiusSpin->setValue(0.01);
    contactRadiusRow->addWidget(contactRadiusSpin, 1);
    layout->addLayout(contactRadiusRow);

    QHBoxLayout* actionRow = new QHBoxLayout();
    applyButton = new QPushButton("Apply Settings", this);
    computeButton = new QPushButton("Compute Contact", this);
    actionRow->addWidget(applyButton);
    actionRow->addWidget(computeButton);
    layout->addLayout(actionRow);

    layout->addStretch();

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
    });
    connect(computeButton, &QPushButton::clicked, this, [this]() {
        requestCompute();
    });
}

void NodeContactPairPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    if (currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

void NodeContactPairPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    refreshFromNode();
}

void NodeContactPairPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

void NodeContactPairPanel::applySettings() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot apply settings for this node.");
        return;
    }

    if (minNormalDotSpin) {
        NodePanelUtils::writeFloatParam(
            nodeGraphBridge, currentNodeId,
            nodegraphparams::contactpair::MinNormalDot,
            minNormalDotSpin->value());
    }
    if (contactRadiusSpin) {
        NodePanelUtils::writeFloatParam(
            nodeGraphBridge, currentNodeId,
            nodegraphparams::contactpair::ContactRadius,
            contactRadiusSpin->value());
    }

    setStatus("Contact pair settings applied.");
}

void NodeContactPairPanel::requestCompute() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot compute contact for this node.");
        return;
    }

    applySettings();

    NodePanelUtils::writeBoolParam(
        nodeGraphBridge, currentNodeId,
        nodegraphparams::contactpair::ComputeRequested, true);

    setStatus("Contact computation requested.");
}

void NodeContactPairPanel::refreshFromNode() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    if (minNormalDotSpin) {
        minNormalDotSpin->setValue(
            NodePanelUtils::readFloatParam(node, nodegraphparams::contactpair::MinNormalDot, -0.65));
    }
    if (contactRadiusSpin) {
        contactRadiusSpin->setValue(
            NodePanelUtils::readFloatParam(node, nodegraphparams::contactpair::ContactRadius, 0.01));
    }

    // Show connected body info from debug data if available.
    if (bodyALabel) {
        bodyALabel->setText("(connected)");
    }
    if (bodyBLabel) {
        bodyBLabel->setText("(connected)");
    }
}

void NodeContactPairPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
