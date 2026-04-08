#include "NodeTransformPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"
#include "NodeTransformParams.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <utility>

namespace {

void configureTranslateSpinBox(QDoubleSpinBox& spinBox) {
    spinBox.setMinimum(-10000.0);
    spinBox.setMaximum(10000.0);
    spinBox.setDecimals(4);
    spinBox.setSingleStep(0.1);
    spinBox.setValue(0.0);
}

void configureRotateSpinBox(QDoubleSpinBox& spinBox) {
    spinBox.setMinimum(-360.0);
    spinBox.setMaximum(360.0);
    spinBox.setDecimals(3);
    spinBox.setSingleStep(1.0);
    spinBox.setSuffix(" deg");
    spinBox.setValue(0.0);
}

void configureScaleSpinBox(QDoubleSpinBox& spinBox) {
    spinBox.setMinimum(-1000.0);
    spinBox.setMaximum(1000.0);
    spinBox.setDecimals(4);
    spinBox.setSingleStep(0.1);
    spinBox.setValue(1.0);
}

}

NodeTransformPanel::NodeTransformPanel(QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const char* axisLabels[3] = {"X", "Y", "Z"};

    QHBoxLayout* translateRow = new QHBoxLayout();
    translateRow->addWidget(new QLabel("Translate:", this));
    for (std::size_t index = 0; index < translateSpinBoxes.size(); ++index) {
        translateRow->addWidget(new QLabel(axisLabels[index], this));
        translateSpinBoxes[index] = new QDoubleSpinBox(this);
        configureTranslateSpinBox(*translateSpinBoxes[index]);
        translateRow->addWidget(translateSpinBoxes[index], 1);
    }
    layout->addLayout(translateRow);

    QHBoxLayout* rotateRow = new QHBoxLayout();
    rotateRow->addWidget(new QLabel("Rotate:", this));
    for (std::size_t index = 0; index < rotateSpinBoxes.size(); ++index) {
        rotateRow->addWidget(new QLabel(axisLabels[index], this));
        rotateSpinBoxes[index] = new QDoubleSpinBox(this);
        configureRotateSpinBox(*rotateSpinBoxes[index]);
        rotateRow->addWidget(rotateSpinBoxes[index], 1);
    }
    layout->addLayout(rotateRow);

    QHBoxLayout* scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel("Scale:", this));
    for (std::size_t index = 0; index < scaleSpinBoxes.size(); ++index) {
        scaleRow->addWidget(new QLabel(axisLabels[index], this));
        scaleSpinBoxes[index] = new QDoubleSpinBox(this);
        configureScaleSpinBox(*scaleSpinBoxes[index]);
        scaleRow->addWidget(scaleSpinBoxes[index], 1);
    }
    layout->addLayout(scaleRow);

    QLabel* hintLabel = new QLabel(
        "Transform composes onto the incoming GeometryData.localToWorld authored in the graph.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    layout->addStretch();

    for (QDoubleSpinBox* spinBox : translateSpinBoxes) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
            onParametersEdited();
        });
    }
    for (QDoubleSpinBox* spinBox : rotateSpinBoxes) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
            onParametersEdited();
        });
    }
    for (QDoubleSpinBox* spinBox : scaleSpinBoxes) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
            onParametersEdited();
        });
    }
}

void NodeTransformPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
}

void NodeTransformPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    const TransformNodeParams params = readTransformNodeParams(node);
    syncingFromNode = true;
    const QSignalBlocker txBlock(translateSpinBoxes[0]);
    const QSignalBlocker tyBlock(translateSpinBoxes[1]);
    const QSignalBlocker tzBlock(translateSpinBoxes[2]);
    const QSignalBlocker rxBlock(rotateSpinBoxes[0]);
    const QSignalBlocker ryBlock(rotateSpinBoxes[1]);
    const QSignalBlocker rzBlock(rotateSpinBoxes[2]);
    const QSignalBlocker sxBlock(scaleSpinBoxes[0]);
    const QSignalBlocker syBlock(scaleSpinBoxes[1]);
    const QSignalBlocker szBlock(scaleSpinBoxes[2]);

    translateSpinBoxes[0]->setValue(params.translateX);
    translateSpinBoxes[1]->setValue(params.translateY);
    translateSpinBoxes[2]->setValue(params.translateZ);
    rotateSpinBoxes[0]->setValue(params.rotateXDegrees);
    rotateSpinBoxes[1]->setValue(params.rotateYDegrees);
    rotateSpinBoxes[2]->setValue(params.rotateZDegrees);
    scaleSpinBoxes[0]->setValue(params.scaleX);
    scaleSpinBoxes[1]->setValue(params.scaleY);
    scaleSpinBoxes[2]->setValue(params.scaleZ);
    syncingFromNode = false;
}

void NodeTransformPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

bool NodeTransformPanel::writeParameters() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update transform settings for this node");
        return false;
    }

    NodeGraphEditor editor(nodeGraphBridge);
    const TransformNodeParams params{
        translateSpinBoxes[0]->value(),
        translateSpinBoxes[1]->value(),
        translateSpinBoxes[2]->value(),
        rotateSpinBoxes[0]->value(),
        rotateSpinBoxes[1]->value(),
        rotateSpinBoxes[2]->value(),
        scaleSpinBoxes[0]->value(),
        scaleSpinBoxes[1]->value(),
        scaleSpinBoxes[2]->value(),
    };
    if (!writeTransformNodeParams(editor, currentNodeId, params)) {
        setStatus("Failed to update transform settings");
        return false;
    }

    return true;
}

void NodeTransformPanel::onParametersEdited() {
    if (syncingFromNode) {
        return;
    }

    if (!currentNodeId.isValid()) {
        setStatus("Cannot update transform settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Transform settings applied");
    }
}

void NodeTransformPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
