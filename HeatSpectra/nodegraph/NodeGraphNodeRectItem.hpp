#pragma once

#include <QColor>
#include <QGraphicsRectItem>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QVariant>

#include <algorithm>
#include <cstdint>

class NodeGraphNodeRectItem : public QGraphicsRectItem {
public:
    NodeGraphNodeRectItem(const QRectF& rect, int basePenColorRole, int hoveredRole)
        : QGraphicsRectItem(rect),
          basePenColorRole(basePenColorRole),
          hoveredRole(hoveredRole) {
    }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override {
        Q_UNUSED(widget);
        if (!option || !painter) {
            QGraphicsRectItem::paint(painter, option, widget);
            return;
        }

        const bool isSelected = (option->state & QStyle::State_Selected) != 0;
        const bool isHovered = data(hoveredRole).toBool();
        bool colorOk = false;
        const uint32_t baseRgba = static_cast<uint32_t>(data(basePenColorRole).toULongLong(&colorOk));
        const QColor baseColor = colorOk ? QColor::fromRgba(baseRgba) : QColor(120, 144, 190);

        QColor borderColor = baseColor;
        qreal borderWidth = 1.4;
        if (isHovered) {
            borderColor = borderColor.lighter(135);
            borderWidth = 2.0;
        }
        if (isSelected) {
            borderColor = QColor(236, 244, 255);
            borderWidth = std::max(borderWidth, 2.2);
        }

        painter->save();
        QPen borderPen(borderColor, borderWidth);
        borderPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(borderPen);
        painter->setBrush(brush());
        painter->drawRect(rect());
        painter->restore();
    }

private:
    int basePenColorRole = 0;
    int hoveredRole = 0;
};
