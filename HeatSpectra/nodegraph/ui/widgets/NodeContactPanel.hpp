#pragma once

#include "NodePanelBase.hpp"

class QCheckBox;
class NodeGraphSliderRow;

class NodeContactPanel final : public NodePanelBase {
public:
    explicit NodeContactPanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    bool writeParameters();
    void onParametersEdited();

    NodeGraphSliderRow* minNormalDotRow = nullptr;
    NodeGraphSliderRow* contactRadiusRow = nullptr;
    QCheckBox* showContactLinesCheckBox = nullptr;
};