#pragma once

#include "NodePanelBase.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

class NodeGroupPanel final : public NodePanelBase {
public:
    explicit NodeGroupPanel(QWidget* parent = nullptr);

    void refreshSourceOptions();

protected:
    void refreshFromNode() override;

private:
    void applySettings();

    QCheckBox* enabledCheckBox = nullptr;
    QComboBox* sourceTypeComboBox = nullptr;
    QComboBox* sourceNameComboBox = nullptr;
    QLineEdit* targetNameLineEdit = nullptr;
    QPushButton* applyButton = nullptr;
};