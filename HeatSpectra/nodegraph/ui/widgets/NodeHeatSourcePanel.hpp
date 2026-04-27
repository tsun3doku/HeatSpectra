#pragma once

#include "NodePanelBase.hpp"

class QLineEdit;
class QPushButton;

class NodeHeatSourcePanel final : public NodePanelBase {
public:
    explicit NodeHeatSourcePanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    void applySettings();

    QLineEdit* temperatureEdit = nullptr;
    QPushButton* applyButton = nullptr;
};