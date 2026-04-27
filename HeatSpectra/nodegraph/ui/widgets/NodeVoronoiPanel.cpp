#include "NodeVoronoiPanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "NodeGraphSliderRow.hpp"
#include "nodegraph/NodeVoronoiParams.hpp"
#include "domain/VoronoiParams.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

NodeVoronoiPanel::NodeVoronoiPanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    const VoronoiParams defaults{};

    cellSizeRow = new NodeGraphSliderRow("Cell Size", this);
    cellSizeRow->setRange(0.0001, 1.0);
    cellSizeRow->setDecimals(6);
    cellSizeRow->setValue(defaults.cellSize);
    layout->addWidget(cellSizeRow);

    voxelResolutionRow = new NodeGraphSliderRow("Voxel Resolution", this);
    voxelResolutionRow->setRange(1.0, 1024.0);
    voxelResolutionRow->setDecimals(0);
    voxelResolutionRow->setValue(static_cast<double>(defaults.voxelResolution));
    layout->addWidget(voxelResolutionRow);

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

    cellSizeRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    voxelResolutionRow->setValueChangedCallback([this](double) { onParametersEdited(); });
    connect(showVoronoiCheckBox, &QCheckBox::toggled, this, [this](bool) { onParametersEdited(); });
    connect(showPointsCheckBox, &QCheckBox::toggled, this, [this](bool) { onParametersEdited(); });
}

void NodeVoronoiPanel::refreshFromNode() {
    if (!canEdit()) {
        return;
    }

    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    const VoronoiNodeParams params = readVoronoiNodeParams(node);
    setSyncing(true);
    cellSizeRow->setValue(params.cellSize);
    voxelResolutionRow->setValue(static_cast<double>(params.voxelResolution));
    showVoronoiCheckBox->setChecked(params.preview.showVoronoi);
    showPointsCheckBox->setChecked(params.preview.showPoints);
    setSyncing(false);
}

bool NodeVoronoiPanel::writeParameters() {
    if (!canEdit()) {
        setStatus("Cannot update Voronoi settings for this node");
        return false;
    }

    NodeGraphEditor editor(bridge());
    const VoronoiNodeParams params{
        cellSizeRow->value(),
        static_cast<int>(voxelResolutionRow->value()),
        {showVoronoiCheckBox->isChecked(), showPointsCheckBox->isChecked()},
    };
    if (!writeVoronoiNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update Voronoi settings");
        return false;
    }
    return true;
}

void NodeVoronoiPanel::onParametersEdited() {
    if (isSyncing()) {
        return;
    }

    if (!currentNodeId().isValid()) {
        setStatus("Cannot update Voronoi settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Voronoi settings applied");
    }
}