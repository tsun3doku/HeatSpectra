#include "NodeVoronoiPanel.hpp"

#include "NodeGraphRegistry.hpp"
#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"
#include "NodePanelUtils.hpp"
#include "NodeVoronoiParams.hpp"
#include "domain/VoronoiParams.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

NodeVoronoiPanel::NodeVoronoiPanel(QWidget* parent)
    : QWidget(parent) {
    const VoronoiParams defaults{};

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* cellSizeRow = new QHBoxLayout();
    cellSizeRow->addWidget(new QLabel("Cell Size:", this));
    cellSizeSpinBox = new QDoubleSpinBox(this);
    cellSizeSpinBox->setMinimum(0.0001);
    cellSizeSpinBox->setMaximum(1.0);
    cellSizeSpinBox->setDecimals(6);
    cellSizeSpinBox->setSingleStep(0.001);
    cellSizeSpinBox->setValue(defaults.cellSize);
    cellSizeRow->addWidget(cellSizeSpinBox, 1);
    layout->addLayout(cellSizeRow);

    QHBoxLayout* voxelResolutionRow = new QHBoxLayout();
    voxelResolutionRow->addWidget(new QLabel("Voxel Resolution:", this));
    voxelResolutionSpinBox = new QSpinBox(this);
    voxelResolutionSpinBox->setMinimum(1);
    voxelResolutionSpinBox->setMaximum(1024);
    voxelResolutionSpinBox->setValue(defaults.voxelResolution);
    voxelResolutionRow->addWidget(voxelResolutionSpinBox, 1);
    layout->addLayout(voxelResolutionRow);

    QLabel* hintLabel = new QLabel(
        "Voronoi domains rebuild automatically when the input geometry or these parameters change.",
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    showVoronoiCheckBox = new QCheckBox("Show Voronoi", this);
    layout->addWidget(showVoronoiCheckBox);

    showPointsCheckBox = new QCheckBox("Show Points", this);
    layout->addWidget(showPointsCheckBox);

    layout->addStretch();

    connect(cellSizeSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
    });
    connect(voxelResolutionSpinBox, &QSpinBox::valueChanged, this, [this](int) {
        onParametersEdited();
    });
    connect(showVoronoiCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
    connect(showPointsCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onParametersEdited();
    });
}

void NodeVoronoiPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
}

void NodeVoronoiPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    NodeGraphNode node{};
    if (!NodePanelUtils::loadNode(nodeGraphBridge, currentNodeId, node)) {
        return;
    }

    syncingFromNode = true;
    const QSignalBlocker cellSizeBlock(cellSizeSpinBox);
    const QSignalBlocker voxelResolutionBlock(voxelResolutionSpinBox);
    const QSignalBlocker showVoronoiBlock(showVoronoiCheckBox);
    const QSignalBlocker showPointsBlock(showPointsCheckBox);
    const VoronoiNodeParams params = readVoronoiNodeParams(node);
    cellSizeSpinBox->setValue(params.cellSize);
    voxelResolutionSpinBox->setValue(params.voxelResolution);
    showVoronoiCheckBox->setChecked(params.preview.showVoronoi);
    showPointsCheckBox->setChecked(params.preview.showPoints);
    syncingFromNode = false;
}

void NodeVoronoiPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

bool NodeVoronoiPanel::writeParameters() {
    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        setStatus("Cannot update Voronoi settings for this node");
        return false;
    }

    NodeGraphEditor editor(nodeGraphBridge);
    const VoronoiNodeParams params{
        cellSizeSpinBox->value(),
        voxelResolutionSpinBox->value(),
        {showVoronoiCheckBox->isChecked(), showPointsCheckBox->isChecked()},
    };
    if (!writeVoronoiNodeParams(editor, currentNodeId, params)) {
        setStatus("Failed to update Voronoi settings");
        return false;
    }
    return true;
}

void NodeVoronoiPanel::onParametersEdited() {
    if (syncingFromNode) {
        return;
    }

    if (!currentNodeId.isValid()) {
        setStatus("Cannot update Voronoi settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Voronoi settings applied");
    }
}

void NodeVoronoiPanel::setStatus(const QString& text) const {
    if (statusSink) {
        statusSink(text);
    }
}
