#include "NodeHeatSourcePanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeHeatSourceParams.hpp"
#include "NodeGraphWidgetStyle.hpp"

#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

NodeHeatSourcePanel::NodeHeatSourcePanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    QHBoxLayout* tempRow = new QHBoxLayout();
    tempRow->addWidget(new QLabel("Temperature:", this));
    temperatureEdit = new QLineEdit(this);
    nodegraphwidgets::styleLineEdit(temperatureEdit);
    auto* validator = new QDoubleValidator(0.0, 10000.0, 2, temperatureEdit);
    validator->setNotation(QDoubleValidator::StandardNotation);
    temperatureEdit->setValidator(validator);
    temperatureEdit->setText("100.00");
    tempRow->addWidget(temperatureEdit, 1);
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

void NodeHeatSourcePanel::applySettings() {
    if (!canEdit()) {
        setStatus("Cannot apply temperature for this node.");
        return;
    }

    NodeGraphEditor editor(bridge());
    HeatSourceNodeParams params{};
    params.temperature = temperatureEdit->text().toDouble();
    if (!writeHeatSourceNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update heat source settings.");
        return;
    }

    setStatus("Heat source temperature applied.");
}

void NodeHeatSourcePanel::refreshFromNode() {
    if (!canEdit()) {
        return;
    }

    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    temperatureEdit->setText(QString::number(readHeatSourceNodeParams(node).temperature, 'f', 2));
}