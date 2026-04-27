#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class NodeGraphSliderItem;
class QString;

class NodeGraphSliderRow final : public QWidget {
public:
    explicit NodeGraphSliderRow(const QString& labelText, QWidget* parent = nullptr);

    void setLabelWidth(int width);
    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setValue(double value);
    double value() const;

    void setValueChangedCallback(std::function<void(double)> callback);

private:
    QLabel* label = nullptr;
    NodeGraphSliderItem* sliderItem = nullptr;
};
