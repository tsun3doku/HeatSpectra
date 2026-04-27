#pragma once

#include "NodePanelBase.hpp"

class QCheckBox;
class NodeGraphSliderRow;

class NodeVoronoiPanel final : public NodePanelBase {
public:
    explicit NodeVoronoiPanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    bool writeParameters();
    void onParametersEdited();

    NodeGraphSliderRow* cellSizeRow = nullptr;
    NodeGraphSliderRow* voxelResolutionRow = nullptr;
    QCheckBox* showVoronoiCheckBox = nullptr;
    QCheckBox* showPointsCheckBox = nullptr;
};