#include "NodeContactPanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeContactParams.hpp"
#include "NodeGraphSliderRow.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

NodeContactPanel::NodeContactPanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    minNormalDotRow = new NodeGraphSliderRow("Min Normal Dot", this);
    minNormalDotRow->setRange(-1.0, 1.0);
    minNormalDotRow->setDecimals(3);
    minNormalDotRow->setValue(-0.65);
    layout->addWidget(minNormalDotRow);

    contactRadiusRow = new NodeGraphSliderRow("Contact Radius", this);
    contactRadiusRow->setRange(0.0005, 10.0);
    contactRadiusRow->setDecimals(4);
    contactRadiusRow->setValue(0.01);
    layout->addWidget(contactRadiusRow);

    showContactLinesCheckBox = new QCheckBox("Show Contact Lines", this);
    layout->addWidget(showContactLinesCheckBox);

    layout->addStretch();

    minNormalDotRow->setValueChangedCallback([this](double) {
        onParametersEdited();
    });
    contactRadiusRow->setValueChangedCallback([this](double) {
        onParametersEdited();
    });
    connect(showContactLinesCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
}

bool NodeContactPanel::writeParameters() {
    if (!canEdit()) {
        setStatus("Cannot update contact settings for this node.");
        return false;
    }

    NodeGraphEditor editor(bridge());
    const ContactNodeParams params{
        minNormalDotRow->value(),
        contactRadiusRow->value(),
        {showContactLinesCheckBox->isChecked()},
    };
    if (!writeContactNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update contact settings.");
        return false;
    }

    return true;
}

void NodeContactPanel::refreshFromNode() {
    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    const ContactNodeParams params = readContactNodeParams(node);
    setSyncing(true);
    minNormalDotRow->setValue(params.minNormalDot);
    contactRadiusRow->setValue(params.contactRadius);
    showContactLinesCheckBox->setChecked(params.preview.showContactLines);
    setSyncing(false);
}

void NodeContactPanel::onParametersEdited() {
    if (isSyncing()) {
        return;
    }

    if (!currentNodeId().isValid()) {
        setStatus("Cannot update contact settings for this node.");
        return;
    }

    if (writeParameters()) {
        setStatus("Contact settings applied.");
    }
}
