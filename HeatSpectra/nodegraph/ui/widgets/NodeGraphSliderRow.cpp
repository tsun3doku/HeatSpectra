#include "NodeGraphSliderRow.hpp"

#include "NodeGraphSliderItem.hpp"
#include "NodeGraphWidgetStyle.hpp"

#include <QHBoxLayout>
#include <QLabel>

NodeGraphSliderRow::NodeGraphSliderRow(const QString& labelText, QWidget* parent)
    : QWidget(parent) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(nodegraphwidgets::sliderRowSpacing);

    label = new QLabel(labelText, this);
    label->setFixedWidth(nodegraphwidgets::sliderLabelWidth);
    layout->addWidget(label);

    sliderItem = new NodeGraphSliderItem(this);
    layout->addWidget(sliderItem, 1);
}

void NodeGraphSliderRow::setLabelWidth(int width) {
    label->setFixedWidth(width);
}

void NodeGraphSliderRow::setRange(double minimum, double maximum) {
    sliderItem->setRange(minimum, maximum);
}

void NodeGraphSliderRow::setDecimals(int decimals) {
    sliderItem->setDecimals(decimals);
}

void NodeGraphSliderRow::setValue(double value) {
    sliderItem->setValue(value);
}

double NodeGraphSliderRow::value() const {
    return sliderItem->value();
}

void NodeGraphSliderRow::setValueChangedCallback(std::function<void(double)> callback) {
    sliderItem->setValueChangedCallback(std::move(callback));
}