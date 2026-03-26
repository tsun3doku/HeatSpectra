#include "NodeVoronoiPanel.hpp"

#include "NodeGraphRegistry.hpp"
#include "NodeGraphBridge.hpp"
#include "NodePanelUtils.hpp"
#include "domain/VoronoiParams.hpp"

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

    layout->addStretch();

    connect(cellSizeSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
        onParametersEdited();
    });
    connect(voxelResolutionSpinBox, &QSpinBox::valueChanged, this, [this](int) {
        onParametersEdited();
    });
}

void NodeVoronoiPanel::bind(NodeGraphBridge* nodeGraphBridgePtr) {
    nodeGraphBridge = nodeGraphBridgePtr;
}

void NodeVoronoiPanel::setNode(NodeGraphNodeId nodeId) {
    currentNodeId = nodeId;

    if (!nodeGraphBridge || !currentNodeId.isValid()) {
        return;
    }

    NodeGraphNode node{};
    if (!nodeGraphBridge->getNode(currentNodeId, node)) {
        return;
    }

    const VoronoiParams defaults{};

    syncingFromNode = true;
    const QSignalBlocker cellSizeBlock(cellSizeSpinBox);
    const QSignalBlocker voxelResolutionBlock(voxelResolutionSpinBox);
    cellSizeSpinBox->setValue(NodePanelUtils::readFloatParam(
        node,
        nodegraphparams::voronoi::CellSize,
        defaults.cellSize));
    voxelResolutionSpinBox->setValue(NodePanelUtils::readIntParam(
        node,
        nodegraphparams::voronoi::VoxelResolution,
        defaults.voxelResolution));
    syncingFromNode = false;
}

void NodeVoronoiPanel::setStatusSink(std::function<void(const QString&)> statusSinkFn) {
    statusSink = std::move(statusSinkFn);
}

bool NodeVoronoiPanel::writeParameters() {
    if (!nodeGraphBridge) {
        setStatus("Cannot update Voronoi settings for this node");
        return false;
    }

    if (!NodePanelUtils::writeFloatParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::voronoi::CellSize,
            cellSizeSpinBox->value())) {
        setStatus("Failed to update Voronoi cell size");
        return false;
    }

    if (!NodePanelUtils::writeIntParam(
            nodeGraphBridge,
            currentNodeId,
            nodegraphparams::voronoi::VoxelResolution,
            voxelResolutionSpinBox->value())) {
        setStatus("Failed to update Voronoi voxel resolution");
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
