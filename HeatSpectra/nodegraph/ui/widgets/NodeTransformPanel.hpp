#pragma once

#include "NodePanelBase.hpp"

#include <array>

class QLineEdit;

class NodeTransformPanel final : public NodePanelBase {
public:
    explicit NodeTransformPanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    bool writeParameters();
    void onParametersEdited();

    std::array<QLineEdit*, 3> translateEdits{};
    std::array<QLineEdit*, 3> rotateEdits{};
    std::array<QLineEdit*, 3> scaleEdits{};
};