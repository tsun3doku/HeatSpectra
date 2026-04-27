#pragma once

#include <QRect>
#include <QWidget>

#include <functional>

class QMouseEvent;
class QDoubleValidator;
class QLineEdit;
class QPaintEvent;
class QResizeEvent;

class NodeGraphSliderItem final : public QWidget {
public:
    explicit NodeGraphSliderItem(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setValue(double value);
    double value() const { return currentValue; }

    void setValueChangedCallback(std::function<void(double)> callback);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRect sliderRect() const;
    double valueFromMouseX(int x) const;
    int handleCenterX() const;
    void applyValue(double value, bool notify);
    void syncLineEdit();
    void commitLineEdit();

    QDoubleValidator* valueValidator = nullptr;
    QLineEdit* valueEdit = nullptr;
    double minimumValue = 0.0;
    double maximumValue = 1.0;
    double currentValue = 0.0;
    int decimalCount = 3;
    bool dragging = false;
    std::function<void(double)> valueChangedCallback;
};
