#include "NodeTransformPanel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphUtils.hpp"

#include "nodegraph/NodeGraphBridge.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeTransformParams.hpp"
#include "NodeGraphWidgetStyle.hpp"

#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {

QLineEdit* createNumericEdit(QWidget* parent, double minimum, double maximum, int decimals) {
    QLineEdit* edit = new QLineEdit(parent);
    nodegraphwidgets::styleLineEdit(edit);
    auto* validator = new QDoubleValidator(minimum, maximum, decimals, edit);
    validator->setNotation(QDoubleValidator::StandardNotation);
    edit->setValidator(validator);
    return edit;
}

}

NodeTransformPanel::NodeTransformPanel(QWidget* parent)
    : NodePanelBase(parent) {
    QVBoxLayout* layout = static_cast<QVBoxLayout*>(this->layout());

    const char* axisLabels[3] = {"X", "Y", "Z"};

    QHBoxLayout* translateRow = new QHBoxLayout();
    translateRow->addWidget(new QLabel("Translate:", this));
    for (std::size_t index = 0; index < translateEdits.size(); ++index) {
        translateRow->addWidget(new QLabel(axisLabels[index], this));
        translateEdits[index] = createNumericEdit(this, -10000.0, 10000.0, 4);
        translateEdits[index]->setText("0");
        translateRow->addWidget(translateEdits[index], 1);
    }
    layout->addLayout(translateRow);

    QHBoxLayout* rotateRow = new QHBoxLayout();
    rotateRow->addWidget(new QLabel("Rotate:", this));
    for (std::size_t index = 0; index < rotateEdits.size(); ++index) {
        rotateRow->addWidget(new QLabel(axisLabels[index], this));
        rotateEdits[index] = createNumericEdit(this, -360.0, 360.0, 3);
        rotateEdits[index]->setText("0");
        rotateRow->addWidget(rotateEdits[index], 1);
    }
    layout->addLayout(rotateRow);

    QHBoxLayout* scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel("Scale:", this));
    for (std::size_t index = 0; index < scaleEdits.size(); ++index) {
        scaleRow->addWidget(new QLabel(axisLabels[index], this));
        scaleEdits[index] = createNumericEdit(this, -1000.0, 1000.0, 4);
        scaleEdits[index]->setText("1");
        scaleRow->addWidget(scaleEdits[index], 1);
    }
    layout->addLayout(scaleRow);

    layout->addStretch();

    for (QLineEdit* edit : translateEdits) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { onParametersEdited(); });
    }
    for (QLineEdit* edit : rotateEdits) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { onParametersEdited(); });
    }
    for (QLineEdit* edit : scaleEdits) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() { onParametersEdited(); });
    }
}

void NodeTransformPanel::refreshFromNode() {
    if (!canEdit()) {
        return;
    }

    NodeGraphNode node{};
    if (!loadCurrentNode(node)) {
        return;
    }

    const TransformNodeParams params = readTransformNodeParams(node);
    setSyncing(true);
    translateEdits[0]->setText(QString::number(params.translateX, 'f', 4));
    translateEdits[1]->setText(QString::number(params.translateY, 'f', 4));
    translateEdits[2]->setText(QString::number(params.translateZ, 'f', 4));
    rotateEdits[0]->setText(QString::number(params.rotateXDegrees, 'f', 3));
    rotateEdits[1]->setText(QString::number(params.rotateYDegrees, 'f', 3));
    rotateEdits[2]->setText(QString::number(params.rotateZDegrees, 'f', 3));
    scaleEdits[0]->setText(QString::number(params.scaleX, 'f', 4));
    scaleEdits[1]->setText(QString::number(params.scaleY, 'f', 4));
    scaleEdits[2]->setText(QString::number(params.scaleZ, 'f', 4));
    setSyncing(false);
}

bool NodeTransformPanel::writeParameters() {
    if (!canEdit()) {
        setStatus("Cannot update transform settings for this node");
        return false;
    }

    const double tx = translateEdits[0]->text().toDouble();
    const double ty = translateEdits[1]->text().toDouble();
    const double tz = translateEdits[2]->text().toDouble();
    const double rx = rotateEdits[0]->text().toDouble();
    const double ry = rotateEdits[1]->text().toDouble();
    const double rz = rotateEdits[2]->text().toDouble();
    const double sx = scaleEdits[0]->text().toDouble();
    const double sy = scaleEdits[1]->text().toDouble();
    const double sz = scaleEdits[2]->text().toDouble();

    NodeGraphEditor editor(bridge());
    const TransformNodeParams params{tx, ty, tz, rx, ry, rz, sx, sy, sz};
    if (!writeTransformNodeParams(editor, currentNodeId(), params)) {
        setStatus("Failed to update transform settings");
        return false;
    }

    return true;
}

void NodeTransformPanel::onParametersEdited() {
    if (isSyncing()) {
        return;
    }

    if (!currentNodeId().isValid()) {
        setStatus("Cannot update transform settings for this node");
        return;
    }

    if (writeParameters()) {
        setStatus("Transform settings applied");
    }
}
