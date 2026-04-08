#include "NodeContactPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"
#include "NodePanelUtils.hpp"
#include "NodeContactParams.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
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
        [this](double) {
            onParametersEdited();
        });
    connect(contactRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) {
            onParametersEdited();
        });
    connect(showContactLinesCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
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

bool NodeContactPanel::writeParameters() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update contact settings for this node.");
        return false;
    }

    NodeGraphEditor editor(nodeGraphBridge);
    const ContactNodeParams params{
        minNormalDotSpin->value(),
        contactRadiusSpin->value(),
        {showContactLinesCheckBox->isChecked()},
    };
    if (!writeContactNodeParams(editor, currentNodeId, params)) {
        setStatus("Failed to update contact settings.");
        return false;
    }

    return true;
}

void NodeContactPanel::refreshFromNode() {
    NodeGraphNode node{};
    if (!NodePanelUtils::loadNode(nodeGraphBridge, currentNodeId, node)) {
        return;
    }

    const ContactNodeParams params = readContactNodeParams(node);
    syncingFromNode = true;
    const QSignalBlocker minNormalDotBlock(minNormalDotSpin);
    const QSignalBlocker contactRadiusBlock(contactRadiusSpin);
    const QSignalBlocker showContactLinesBlock(showContactLinesCheckBox);
    if (minNormalDotSpin) {
        minNormalDotSpin->setValue(params.minNormalDot);
    }
    if (contactRadiusSpin) {
        contactRadiusSpin->setValue(params.contactRadius);
    }
    if (showContactLinesCheckBox) {
        showContactLinesCheckBox->setChecked(params.preview.showContactLines);
    }
    syncingFromNode = false;
    if (emitterLabel) {
        emitterLabel->setText("(connected)");
    }
    if (receiverLabel) {
        receiverLabel->setText("(connected)");
    }
}

void NodeContactPanel::onParametersEdited() {
    if (syncingFromNode) {
        return;
    }

    if (!currentNodeId.isValid()) {
        setStatus("Cannot update contact settings for this node.");
        return;
    }

    if (writeParameters()) {
        setStatus("Contact settings applied.");
    }
}

void NodeContactPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
