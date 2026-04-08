#include "NodeRemeshPanel.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"
#include "NodeRemeshParams.hpp"
#include "domain/RemeshParams.hpp"

#include <QCheckBox>
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

    remeshOverlayCheckBox = new QCheckBox("Remesh Overlay", this);
    layout->addWidget(remeshOverlayCheckBox);

    faceNormalsCheckBox = new QCheckBox("Face Normals", this);
    layout->addWidget(faceNormalsCheckBox);

    vertexNormalsCheckBox = new QCheckBox("Vertex Normals", this);
    layout->addWidget(vertexNormalsCheckBox);

    QHBoxLayout* normalLengthRow = new QHBoxLayout();
    normalLengthRow->addWidget(new QLabel("Normal Length:", this));
    normalLengthSpinBox = new QDoubleSpinBox(this);
    normalLengthSpinBox->setMinimum(0.001);
    normalLengthSpinBox->setMaximum(10.0);
    normalLengthSpinBox->setSingleStep(0.01);
    normalLengthSpinBox->setDecimals(3);
    normalLengthSpinBox->setValue(0.05);
    normalLengthRow->addWidget(normalLengthSpinBox, 1);
    layout->addLayout(normalLengthRow);

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
    connect(remeshOverlayCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
    connect(faceNormalsCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
    connect(vertexNormalsCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
    connect(normalLengthSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
    });
}

void NodeRemeshPanel::bind(NodeGraphBridge* bridge) {
    nodeGraphBridge = bridge;
}

void NodeRemeshPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    NodeGraphNode node{};
    if (!nodeGraphBridge || !currentNodeId.isValid() || !nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    const RemeshNodeParams params = readRemeshNodeParams(node);

    syncingFromNode = true;
    const QSignalBlocker iterationsBlock(iterationsSpinBox);
    const QSignalBlocker minAngleBlock(minAngleSpinBox);
    const QSignalBlocker maxEdgeBlock(maxEdgeLengthSpinBox);
    const QSignalBlocker stepBlock(stepSizeSpinBox);
    const QSignalBlocker remeshOverlayBlock(remeshOverlayCheckBox);
    const QSignalBlocker faceNormalsBlock(faceNormalsCheckBox);
    const QSignalBlocker vertexNormalsBlock(vertexNormalsCheckBox);
    const QSignalBlocker normalLengthBlock(normalLengthSpinBox);
    iterationsSpinBox->setValue(params.iterations);
    minAngleSpinBox->setValue(params.minAngleDegrees);
    maxEdgeLengthSpinBox->setValue(params.maxEdgeLength);
    stepSizeSpinBox->setValue(params.stepSize);
    remeshOverlayCheckBox->setChecked(params.preview.showRemeshOverlay);
    faceNormalsCheckBox->setChecked(params.preview.showFaceNormals);
    vertexNormalsCheckBox->setChecked(params.preview.showVertexNormals);
    normalLengthSpinBox->setValue(params.normalLength);
    syncingFromNode = false;
}

void NodeRemeshPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink = std::move(sink);
}

bool NodeRemeshPanel::writeParameters() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update settings for this node");
        return false;
    }

    NodeGraphEditor editor(nodeGraphBridge);
    const RemeshNodeParams params{
        iterationsSpinBox->value(),
        minAngleSpinBox->value(),
        maxEdgeLengthSpinBox->value(),
        stepSizeSpinBox->value(),
        {
            remeshOverlayCheckBox->isChecked(),
            faceNormalsCheckBox->isChecked(),
            vertexNormalsCheckBox->isChecked(),
        },
        normalLengthSpinBox->value(),
    };

    if (!writeRemeshNodeParams(editor, currentNodeId, params)) {
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
