#include "NodeGraphSliderItem.hpp"
#include "NodeGraphWidgetStyle.hpp"

#include <QDoubleValidator>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSignalBlocker>

#include <algorithm>

static double clampValue(double value, double minimum, double maximum) {
    return std::max(minimum, std::min(maximum, value));
}

NodeGraphSliderItem::NodeGraphSliderItem(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(nodegraphwidgets::sliderMinimumHeight);
    setMouseTracking(true);

    valueEdit = new QLineEdit(this);
    nodegraphwidgets::styleLineEdit(valueEdit);
    valueEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueValidator = new QDoubleValidator(minimumValue, maximumValue, decimalCount, valueEdit);
    valueEdit->setValidator(valueValidator);

    connect(valueEdit, &QLineEdit::editingFinished, this, [this]() {
        commitLineEdit();
    });

    syncLineEdit();
}

void NodeGraphSliderItem::setRange(double minimum, double maximum) {
    minimumValue = std::min(minimum, maximum);
    maximumValue = std::max(minimum, maximum);

    valueValidator->setBottom(minimumValue);
    valueValidator->setTop(maximumValue);
    valueValidator->setDecimals(decimalCount);

    applyValue(currentValue, false);
}

void NodeGraphSliderItem::setDecimals(int decimals) {
    decimalCount = std::max(0, decimals);

    valueValidator->setBottom(minimumValue);
    valueValidator->setTop(maximumValue);
    valueValidator->setDecimals(decimalCount);

    syncLineEdit();
}

void NodeGraphSliderItem::setValue(double value) {
    applyValue(value, false);
}

void NodeGraphSliderItem::setValueChangedCallback(std::function<void(double)> callback) {
    valueChangedCallback = std::move(callback);
}

void NodeGraphSliderItem::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    const QRect drawRect = sliderRect();
    if (drawRect.width() <= 0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF trackRect(
        static_cast<qreal>(drawRect.left()),
        static_cast<qreal>((height() - nodegraphwidgets::sliderTrackHeight) / 2),
        static_cast<qreal>(drawRect.width()),
        static_cast<qreal>(nodegraphwidgets::sliderTrackHeight));
    const qreal trackRadius = static_cast<qreal>(nodegraphwidgets::sliderTrackRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(nodegraphwidgets::colorSliderTrack);
    painter.drawRoundedRect(trackRect, trackRadius, trackRadius);

    const int centerX = handleCenterX();
    const QRectF activeTrackRect(
        static_cast<qreal>(drawRect.left()),
        trackRect.top(),
        static_cast<qreal>(std::max(0, centerX - drawRect.left())),
        trackRect.height());
    if (activeTrackRect.width() > 0.0) {
        painter.setBrush(nodegraphwidgets::colorSliderTrackActive);
        painter.drawRoundedRect(activeTrackRect, trackRadius, trackRadius);
    }

    const int maskDiameter = nodegraphwidgets::sliderKnobDiameter + nodegraphwidgets::sliderKnobMaskPadding * 2;
    const QRectF maskRect(
        static_cast<qreal>(centerX - maskDiameter / 2),
        static_cast<qreal>((height() - maskDiameter) / 2),
        static_cast<qreal>(maskDiameter),
        static_cast<qreal>(maskDiameter));
    painter.setBrush(nodegraphwidgets::colorSliderKnobMask);
    painter.drawEllipse(maskRect);

    const QRectF knobRect(
        static_cast<qreal>(centerX - nodegraphwidgets::sliderKnobDiameter / 2),
        static_cast<qreal>((height() - nodegraphwidgets::sliderKnobDiameter) / 2),
        static_cast<qreal>(nodegraphwidgets::sliderKnobDiameter),
        static_cast<qreal>(nodegraphwidgets::sliderKnobDiameter));
    painter.setBrush(nodegraphwidgets::colorSliderKnobFill);
    painter.setPen(QPen(nodegraphwidgets::colorSliderKnobStroke, nodegraphwidgets::sliderKnobBorderWidth));
    painter.drawEllipse(knobRect);
}

void NodeGraphSliderItem::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    valueEdit->setGeometry(
        width() - nodegraphwidgets::sliderValueFieldWidth,
        0,
        nodegraphwidgets::sliderValueFieldWidth,
        height());
}

void NodeGraphSliderItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (!sliderRect().contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }

    dragging = true;
    applyValue(valueFromMouseX(static_cast<int>(event->position().x())), true);
    event->accept();
}

void NodeGraphSliderItem::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    applyValue(valueFromMouseX(static_cast<int>(event->position().x())), true);
    event->accept();
}

void NodeGraphSliderItem::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging) {
        dragging = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

QRect NodeGraphSliderItem::sliderRect() const {
    const int knobRadius = nodegraphwidgets::sliderKnobDiameter / 2;
    const int leftEdge = nodegraphwidgets::sliderHorizontalInset + knobRadius;
    const int rightEdge = std::max(
        leftEdge,
        width() - nodegraphwidgets::sliderValueFieldWidth - nodegraphwidgets::sliderRightPadding - knobRadius);
    return QRect(
        leftEdge,
        0,
        std::max(0, rightEdge - leftEdge),
        height());
}

double NodeGraphSliderItem::valueFromMouseX(int x) const {
    const QRect rect = sliderRect();
    if (rect.width() <= 0 || maximumValue <= minimumValue) {
        return minimumValue;
    }

    const double t = static_cast<double>(std::clamp(x, rect.left(), rect.right()) - rect.left()) /
        static_cast<double>(std::max(1, rect.width()));
    return minimumValue + (maximumValue - minimumValue) * t;
}

int NodeGraphSliderItem::handleCenterX() const {
    const QRect rect = sliderRect();
    if (rect.width() <= 0 || maximumValue <= minimumValue) {
        return rect.left();
    }

    const double t = (currentValue - minimumValue) / (maximumValue - minimumValue);
    return rect.left() + static_cast<int>(t * static_cast<double>(rect.width()));
}

void NodeGraphSliderItem::applyValue(double value, bool notify) {
    const double clampedValue = clampValue(value, minimumValue, maximumValue);
    currentValue = clampedValue;
    syncLineEdit();
    update();

    if (notify && valueChangedCallback) {
        valueChangedCallback(currentValue);
    }
}

void NodeGraphSliderItem::syncLineEdit() {
    const QSignalBlocker blocker(valueEdit);
    valueEdit->setText(QString::number(currentValue, 'f', decimalCount));
}

void NodeGraphSliderItem::commitLineEdit() {
    bool ok = false;
    const double parsedValue = valueEdit->text().toDouble(&ok);
    if (!ok) {
        syncLineEdit();
        return;
    }

    applyValue(parsedValue, true);
}
