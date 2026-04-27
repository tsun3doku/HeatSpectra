#include "NodeRemeshPanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeRemeshParams.hpp"
#include "domain/RemeshParams.hpp"
#include "NodeGraphSliderRow.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

NodeRemeshPanel::NodeRemeshPanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    const RemeshParams defaults{};

    iterationsRow = new NodeGraphSliderRow("Iterations", this);
    iterationsRow->setRange(1.0, 1000.0);
    iterationsRow->setDecimals(0);
    iterationsRow->setValue(static_cast<double>(defaults.iterations));
    layout->addWidget(iterationsRow);

    minAngleRow = new NodeGraphSliderRow("Min Angle", this);
    minAngleRow->setRange(0.0, 60.0);
    minAngleRow->setDecimals(1);
    minAngleRow->setValue(defaults.minAngleDegrees);
    layout->addWidget(minAngleRow);

    maxEdgeLengthRow = new NodeGraphSliderRow("Max Edge Length", this);
    maxEdgeLengthRow->setRange(0.001, 10.0);
    maxEdgeLengthRow->setDecimals(4);
    maxEdgeLengthRow->setValue(defaults.maxEdgeLength);
    layout->addWidget(maxEdgeLengthRow);

    stepSizeRow = new NodeGraphSliderRow("Step Size", this);
    stepSizeRow->setRange(0.01, 1.0);
    stepSizeRow->setDecimals(2);
    stepSizeRow->setValue(defaults.stepSize);
    layout->addWidget(stepSizeRow);

    remeshOverlayCheckBox = new QCheckBox("Remesh Overlay", this);
    layout->addWidget(remeshOverlayCheckBox);

    faceNormalsCheckBox = new QCheckBox("Face Normals", this);
    layout->addWidget(faceNormalsCheckBox);

    vertexNormalsCheckBox = new QCheckBox("Vertex Normals", this);
    layout->addWidget(vertexNormalsCheckBox);

    normalLengthRow = new NodeGraphSliderRow("Normal Length", this);
    normalLengthRow->setRange(0.001, 10.0);
    normalLengthRow->setDecimals(3);
    normalLengthRow->setValue(0.05);
    layout->addWidget(normalLengthRow);

    layout->addStretch();

    iterationsRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    minAngleRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    maxEdgeLengthRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    stepSizeRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    connect(remeshOverlayCheckBox, &QCheckBox::toggled, this, [this](bool) { onParametersEdited(); });
    connect(faceNormalsCheckBox, &QCheckBox::toggled, this, [this](bool) { onParametersEdited(); });
    connect(vertexNormalsCheckBox, &QCheckBox::toggled, this, [this](bool) { onParametersEdited(); });
    normalLengthRow->setValueChangedCallback([this](double) { onParametersEdited(); });
}

void NodeRemeshPanel::refreshFromNode() {
    if (!canEdit()) {
        return;
    }

    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    const RemeshNodeParams params = readRemeshNodeParams(node);

    setSyncing(true);
    iterationsRow->setValue(static_cast<double>(params.iterations));
    minAngleRow->setValue(params.minAngleDegrees);
    maxEdgeLengthRow->setValue(params.maxEdgeLength);
    stepSizeRow->setValue(params.stepSize);
    remeshOverlayCheckBox->setChecked(params.preview.showRemeshOverlay);
    faceNormalsCheckBox->setChecked(params.preview.showFaceNormals);
    vertexNormalsCheckBox->setChecked(params.preview.showVertexNormals);
    normalLengthRow->setValue(params.normalLength);
    setSyncing(false);
}

bool NodeRemeshPanel::writeParameters() {
    if (!canEdit()) {
        setStatus("Cannot update settings for this node");
        return false;
    }

    NodeGraphEditor editor(bridge());
    const RemeshNodeParams params{
        static_cast<int>(iterationsRow->value()),
        minAngleRow->value(),
        maxEdgeLengthRow->value(),
        stepSizeRow->value(),
        {
            remeshOverlayCheckBox->isChecked(),
            faceNormalsCheckBox->isChecked(),
            vertexNormalsCheckBox->isChecked(),
        },
        normalLengthRow->value(),
    };

    if (!writeRemeshNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update remesh settings");
        return false;
    }

    return true;
}

void NodeRemeshPanel::onParametersEdited() {
    if (isSyncing()) {
        return;
    }

    if (!currentNodeId().isValid()) {
        setStatus("Cannot update remesh settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Remesh settings applied");
    }
}