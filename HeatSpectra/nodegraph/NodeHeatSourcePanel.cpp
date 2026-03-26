#include "NodeHeatSourcePanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

NodeHeatSourcePanel::NodeHeatSourcePanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* tempRow = new QHBoxLayout();
    temperatureLabel = new QLabel("Temperature:", this);
    tempRow->addWidget(temperatureLabel);
    temperatureSpin = new QDoubleSpinBox(this);
    temperatureSpin->setRange(0.0, 10000.0);
    temperatureSpin->setSingleStep(10.0);
    temperatureSpin->setDecimals(2);
    temperatureSpin->setValue(100.0);
    tempRow->addWidget(temperatureSpin, 1);
    layout->addLayout(tempRow);

    QHBoxLayout* actionRow = new QHBoxLayout();
    applyButton = new QPushButton("Apply Temperature", this);
    actionRow->addWidget(applyButton);
    layout->addLayout(actionRow);

    layout->addStretch();

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
    });
}

void NodeHeatSourcePanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
    if (currentNodeId.isValid()) {
        setNode(currentNodeId);
    }
}

void NodeHeatSourcePanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    refreshFromNode();
}

void NodeHeatSourcePanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

void NodeHeatSourcePanel::applySettings() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot apply temperature for this node.");
        return;
    }

    if (temperatureSpin) {
        NodePanelUtils::writeFloatParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::heatsource::Temperature,
            temperatureSpin->value());
    }

    setStatus("Heat source temperature applied.");
}

void NodeHeatSourcePanel::refreshFromNode() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    if (temperatureSpin) {
        temperatureSpin->setValue(
            NodePanelUtils::readFloatParam(
                node,
                nodegraphparams::heatsource::Temperature,
                100.0));
    }
}

void NodeHeatSourcePanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
