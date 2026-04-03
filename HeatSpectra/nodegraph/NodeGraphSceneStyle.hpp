#pragma once

#include "NodeGraphTypes.hpp"
#include "render/SceneColorSpace.hpp"

#include <array>

#include <QColor>
#include <QGraphicsItem>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

namespace nodegraphscene {

constexpr qreal nodeWidth = 180.0;
constexpr qreal nodeHeight = 94.0;
constexpr qreal nodeHeaderHeight = nodeHeight * 0.25;
constexpr qreal socketStartY = nodeHeaderHeight + 16.0;
constexpr qreal socketSpacing = 18.0;
constexpr qreal socketRadius = 5.0;

const std::array<float, 4>& clearColor();
QColor categoryColor(NodeGraphNodeCategory category);
QColor valueTypeColor(NodeGraphValueType valueType);
qreal socketY(std::size_t index);
QPointF inputAnchor(const QRectF& rect);
QPointF outputAnchor(const QRectF& rect);
QPainterPath buildEdgePath(
    const QPointF& src,
    const QPointF& dst,
    NodeGraphSocketDirection srcDirection = NodeGraphSocketDirection::Output,
    NodeGraphSocketDirection dstDirection = NodeGraphSocketDirection::Input);
void setDecorativeItemFlags(QGraphicsItem* item);

} 
