#include "NodeTransformPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"

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

    translateSpinBoxes[0]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::TranslateX, 0.0));
    translateSpinBoxes[1]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::TranslateY, 0.0));
    translateSpinBoxes[2]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::TranslateZ, 0.0));
    rotateSpinBoxes[0]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::RotateXDegrees, 0.0));
    rotateSpinBoxes[1]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::RotateYDegrees, 0.0));
    rotateSpinBoxes[2]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::RotateZDegrees, 0.0));
    scaleSpinBoxes[0]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::ScaleX, 1.0));
    scaleSpinBoxes[1]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::ScaleY, 1.0));
    scaleSpinBoxes[2]->setValue(NodePanelUtils::readFloatParam(node, nodegraphparams::transform::ScaleZ, 1.0));
    syncingFromNode = false;
}

void NodeTransformPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

bool NodeTransformPanel::writeParameters() {
    if (!nodeGraphBridge) {
        setStatus("Cannot update transform settings for this node");
        return false;
    }

    if (!NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::TranslateX, translateSpinBoxes[0]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::TranslateY, translateSpinBoxes[1]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::TranslateZ, translateSpinBoxes[2]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::RotateXDegrees, rotateSpinBoxes[0]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::RotateYDegrees, rotateSpinBoxes[1]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::RotateZDegrees, rotateSpinBoxes[2]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::ScaleX, scaleSpinBoxes[0]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::ScaleY, scaleSpinBoxes[1]->value()) ||
        !NodePanelUtils::writeFloatParam(nodeGraphBridge, currentNodeId, nodegraphparams::transform::ScaleZ, scaleSpinBoxes[2]->value())) {
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
