#pragma once

#include "NodePanelBase.hpp"

class QCheckBox;
class NodeGraphSliderRow;

class NodeRemeshPanel final : public NodePanelBase {
public:
    explicit NodeRemeshPanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    bool writeParameters();
    void onParametersEdited();

    NodeGraphSliderRow* iterationsRow = nullptr;
    NodeGraphSliderRow* minAngleRow = nullptr;
    NodeGraphSliderRow* maxEdgeLengthRow = nullptr;
    NodeGraphSliderRow* stepSizeRow = nullptr;
    QCheckBox* remeshOverlayCheckBox = nullptr;
    QCheckBox* faceNormalsCheckBox = nullptr;
    QCheckBox* vertexNormalsCheckBox = nullptr;
    NodeGraphSliderRow* normalLengthRow = nullptr;
};