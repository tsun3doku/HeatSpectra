#include "NodeContactPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

NodeContactPanel::NodeContactPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* emitterRow = new QHBoxLayout();
    emitterRow->addWidget(new QLabel("Emitter:", this));
    emitterLabel = new QLabel("(none)", this);
    emitterRow->addWidget(emitterLabel, 1);
    layout->addLayout(emitterRow);

    QHBoxLayout* receiverRow = new QHBoxLayout();
    receiverRow->addWidget(new QLabel("Receiver:", this));
    receiverLabel = new QLabel("(none)", this);
    receiverRow->addWidget(receiverLabel, 1);
    layout->addLayout(receiverRow);

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
    contactRadiusSpin->setRange(0.0005, 10.0);
    contactRadiusSpin->setSingleStep(0.005);
    contactRadiusSpin->setDecimals(4);
    contactRadiusSpin->setValue(0.01);
    contactRadiusRow->addWidget(contactRadiusSpin, 1);
    layout->addLayout(contactRadiusRow);

    showContactLinesCheckBox = new QCheckBox("Show Contact Lines", this);
    layout->addWidget(showContactLinesCheckBox);

    layout->addStretch();

    connect(minNormalDotSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) {
            writeMinNormalDot(value);
        });
    connect(contactRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) {
            writeContactRadius(value);
        });
    connect(showContactLinesCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (!nodeGraphBridge || !currentNodeId.isValid()) {
            setStatus("Cannot update contact settings for this node.");
            return;
        }

        if (!NodePanelUtils::writeBoolParam(
                nodeGraphBridge,
                currentNodeId,
                nodegraphparams::contact::ShowContactLines,
                checked)) {
            setStatus("Failed to update contact preview settings.");
            return;
        }

        setStatus("Contact settings applied.");
    });
}

void NodeContactPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    if (currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

void NodeContactPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    refreshFromNode();
}

void NodeContactPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

void NodeContactPanel::writeMinNormalDot(double value) {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update contact settings for this node.");
        return;
    }

    NodePanelUtils::writeFloatParam(
        nodeGraphBridge, currentNodeId,
        nodegraphparams::contact::MinNormalDot,
        value);
}

void NodeContactPanel::writeContactRadius(double value) {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update contact settings for this node.");
        return;
    }

    NodePanelUtils::writeFloatParam(
        nodeGraphBridge, currentNodeId,
        nodegraphparams::contact::ContactRadius,
        value);
}

void NodeContactPanel::refreshFromNode() {
    NodeGraphNode node{};
    if (!NodePanelUtils::loadNode(nodeGraphBridge, currentNodeId, node)) {
        return;
    }

    if (minNormalDotSpin) {
        minNormalDotSpin->setValue(
            NodePanelUtils::readFloatParam(node, nodegraphparams::contact::MinNormalDot, -0.65));
    }
    if (contactRadiusSpin) {
        contactRadiusSpin->setValue(
            NodePanelUtils::readFloatParam(node, nodegraphparams::contact::ContactRadius, 0.01));
    }
    if (showContactLinesCheckBox) {
        showContactLinesCheckBox->setChecked(
            NodePanelUtils::readBoolParam(node, nodegraphparams::contact::ShowContactLines, false));
    }
    if (emitterLabel) {
        emitterLabel->setText("(connected)");
    }
    if (receiverLabel) {
        receiverLabel->setText("(connected)");
    }
}

void NodeContactPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
