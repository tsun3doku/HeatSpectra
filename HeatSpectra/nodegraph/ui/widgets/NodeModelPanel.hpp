#pragma once

#include "NodePanelBase.hpp"

class QLineEdit;
class QPushButton;

class NodeModelPanel final : public NodePanelBase {
public:
    explicit NodeModelPanel(QWidget* parent = nullptr);

protected:
    void refreshFromNode() override;

private:
    bool writeParameters();
    void browseModelFile();
    void applySettings();

    QLineEdit* pathLineEdit = nullptr;
    QPushButton* browseButton = nullptr;
};