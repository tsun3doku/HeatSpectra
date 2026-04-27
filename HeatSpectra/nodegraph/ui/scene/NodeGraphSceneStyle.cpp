#include "NodeGraphSceneStyle.hpp"

#include <algorithm>
#include <cmath>

namespace nodegraphscene {

QColor valueTypeColor(NodeGraphValueType valueType) {
    switch (valueType) {
    case NodeGraphValueType::None:
        return QColor(164, 172, 186);
    case NodeGraphValueType::Mesh:
        return QColor(105, 164, 231);
    case NodeGraphValueType::Emitter:
        return QColor(239, 124, 76);
    case NodeGraphValueType::Receiver:
        return QColor(246, 177, 92);
    case NodeGraphValueType::Volume:
        return QColor(86, 196, 160);
    case NodeGraphValueType::Field:
        return QColor(84, 178, 232);
    case NodeGraphValueType::Vector3:
        return QColor(106, 198, 236);
    case NodeGraphValueType::ScalarFloat:
        return QColor(239, 171, 89);
    case NodeGraphValueType::ScalarInt:
        return QColor(232, 145, 80);
    case NodeGraphValueType::ScalarBool:
        return QColor(233, 218, 104);
    }

    return QColor(164, 172, 186);
}

QColor nodeShellColor() {
    return QColor(188, 190, 196);
}

QColor nodeShellOutlineColor() {
    return QColor(22, 23, 23);
}

QColor nodeCenterFillColor() {
    return QColor(121, 122, 158);
}

QColor selectedBorderColor() {
    return QColor(72, 183, 255);
}

QColor titleColor() {
    return QColor(220, 220, 225);
}

QColor frozenCapActiveColor() {
    return QColor(104, 222, 236);
}

QColor frozenCapInactiveColor() {
    return QColor(121, 122, 158);
}

QColor displayCapActiveColor() {
    return QColor(132, 224, 94);
}

QColor displayCapInactiveColor() {
    return QColor(121, 122, 158);
}

QColor socketBorderColor() {
    return QColor(22, 23, 23);
}

QColor edgeDefaultColor() {
    return QColor(120, 200, 255);
}

QColor dragPreviewColor() {
    return QColor(166, 206, 255);
}

QRectF nodeRect() {
    return QRectF(0.0, 0.0, nodeWidth, nodeHeight);
}

QRectF outerFrameRect() {
    return nodeRect().adjusted(
        nodeOuterBorderWidth * 0.5,
        nodeOuterBorderWidth * 0.5,
        -nodeOuterBorderWidth * 0.5,
        -nodeOuterBorderWidth * 0.5);
}

QRectF segmentBoundsRect() {
    const QRectF outer = outerFrameRect();
    const qreal capHeight = nodeHeight - nodeOuterBorderWidth;
    const qreal cy = outer.top() + (outer.height() - capHeight) * 0.5;
    return QRectF(outer.left(), cy, outer.width(), capHeight);
}

QRectF centerPanelRect() {
    const QRectF outer = segmentBoundsRect();
    const qreal centerWidth = outer.width() - capWidth * 2.0;
    return QRectF(
        outer.left() + capWidth,
        outer.top(),
        centerWidth,
        outer.height());
}

QRectF leftCapRect() {
    const QRectF outer = segmentBoundsRect();
    return QRectF(
        outer.left(),
        outer.top(),
        capWidth,
        outer.height());
}

QRectF rightCapRect() {
    const QRectF outer = segmentBoundsRect();
    return QRectF(
        outer.right() - capWidth,
        outer.top(),
        capWidth,
        outer.height());
}

QRectF iconRect() {
    const QRectF center = centerPanelRect();
    return QRectF(
        center.center().x() - iconBoxWidth * 0.5,
        center.center().y() - iconBoxHeight * 0.5,
        iconBoxWidth,
        iconBoxHeight);
}

QPointF titlePosition(const QRectF& titleBounds) {
    return QPointF(
        rightCapRect().left(),
        outerFrameRect().top() - titleBounds.height() - titleTopOffset);
}

QPainterPath leftCapPath() {
    const QRectF rect = leftCapRect();
    const qreal radius = std::min(roundCorners, std::min(rect.width(), rect.height()) * 0.5);

    QPainterPath path;
    path.moveTo(rect.right(), rect.top());
    path.lineTo(rect.left() + radius, rect.top());
    path.quadTo(rect.left(), rect.top(), rect.left(), rect.top() + radius);
    path.lineTo(rect.left(), rect.bottom() - radius);
    path.quadTo(rect.left(), rect.bottom(), rect.left() + radius, rect.bottom());
    path.lineTo(rect.right(), rect.bottom());
    path.closeSubpath();
    return path;
}

QPainterPath rightCapPath() {
    const QRectF rect = rightCapRect();
    const qreal radius = std::min(roundCorners, std::min(rect.width(), rect.height()) * 0.5);

    QPainterPath path;
    path.moveTo(rect.left(), rect.top());
    path.lineTo(rect.right() - radius, rect.top());
    path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + radius);
    path.lineTo(rect.right(), rect.bottom() - radius);
    path.quadTo(rect.right(), rect.bottom(), rect.right() - radius, rect.bottom());
    path.lineTo(rect.left(), rect.bottom());
    path.closeSubpath();
    return path;
}

QPainterPath centerPanelPath() {
    const QRectF rect = centerPanelRect();
    QPainterPath path;
    path.addRect(rect);
    return path;
}

qreal socketTopY() {
    return outerFrameRect().top() - socketRadius * socketDistance;
}

qreal socketBottomY() {
    return outerFrameRect().bottom() + socketRadius * socketDistance;
}

namespace {

qreal socketX(std::size_t index, std::size_t total, qreal minX, qreal maxX) {
    if (total <= 1) {
        return (minX + maxX) * 0.5;
    }

    const qreal t = static_cast<qreal>(index) / static_cast<qreal>(total - 1);
    return minX + (maxX - minX) * t;
}

}

QPointF inputSocketPosition(std::size_t index, std::size_t total) {
    const QRectF panel = centerPanelRect();
    const qreal minX = outerFrameRect().left() + capWidth + socketRowInset;
    const qreal maxX = panel.center().x() - socketRowInset;
    return QPointF(socketX(index, total, minX, std::max(minX, maxX)), socketTopY());
}

QPointF outputSocketPosition(std::size_t index, std::size_t total) {
    const QRectF panel = centerPanelRect();
    const qreal minX = panel.center().x() + socketRowInset;
    const qreal maxX = outerFrameRect().right() - capWidth - socketRowInset;
    return QPointF(socketX(index, total, std::min(minX, maxX), maxX), socketBottomY());
}

NodeHitRegion hitRegionAt(const QPointF& localPos) {
    if (!nodeRect().contains(localPos)) {
        return NodeHitRegion::None;
    }
    if (leftCapRect().contains(localPos)) {
        return NodeHitRegion::LeftCap;
    }
    if (rightCapRect().contains(localPos)) {
        return NodeHitRegion::RightCap;
    }
    return NodeHitRegion::Body;
}

QPointF inputAnchor(const QRectF& rect) {
    return QPointF(rect.left() + capWidth, rect.top());
}

QPointF outputAnchor(const QRectF& rect) {
    return QPointF(rect.right() - capWidth, rect.bottom());
}

QPainterPath buildEdgePath(
    const QPointF& src,
    const QPointF& dst,
    NodeGraphSocketDirection srcDirection,
    NodeGraphSocketDirection dstDirection) {
    const qreal dy = dst.y() - src.y();
    const qreal dx = dst.x() - src.x();
    const qreal baseTangent = std::max(edgeMinTangent, std::min(edgeMaxTangent, std::fabs(dy) * edgeTangentVertScale + std::fabs(dx) * edgeTangentHorzScale));

    const qreal srcSign = (srcDirection == NodeGraphSocketDirection::Output) ? 1.0 : -1.0;
    const qreal dstSign = (dstDirection == NodeGraphSocketDirection::Input) ? -1.0 : 1.0;

    QPainterPath path(src);
    path.cubicTo(
        QPointF(src.x(), src.y() + srcSign * baseTangent),
        QPointF(dst.x(), dst.y() + dstSign * baseTangent),
        dst);
    return path;
}

void setDecorativeItemFlags(QGraphicsItem* item) {
    if (!item) {
        return;
    }

    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setFlag(QGraphicsItem::ItemIsSelectable, false);
    item->setFlag(QGraphicsItem::ItemIsMovable, false);
}

}
