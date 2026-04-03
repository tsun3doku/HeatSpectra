#include "NodeGraphSceneStyle.hpp"

#include <algorithm>
#include <cmath>

namespace nodegraphscene {

const std::array<float, 4>& clearColor() {
    static const std::array<float, 4> clearColorCache = clearColorSRGBA();
    return clearColorCache;
}

QColor categoryColor(NodeGraphNodeCategory category) {
    switch (category) {
    case NodeGraphNodeCategory::Model:
        return QColor(105, 92, 210);
    case NodeGraphNodeCategory::PointSurface:
        return QColor(72, 172, 96);
    case NodeGraphNodeCategory::Meshing:
        return QColor(196, 62, 62);
    case NodeGraphNodeCategory::System:
        return QColor(219, 136, 46);
    case NodeGraphNodeCategory::Custom:
        return QColor(88, 100, 126);
    }

    return QColor(88, 100, 126);
}

QColor valueTypeColor(NodeGraphValueType valueType) {
    switch (valueType) {
    case NodeGraphValueType::None:
        return QColor(152, 162, 180);
    case NodeGraphValueType::Mesh:
        return QColor(113, 104, 232);
    case NodeGraphValueType::Emitter:
        return QColor(226, 117, 56);
    case NodeGraphValueType::Receiver:
        return QColor(240, 166, 66);
    case NodeGraphValueType::Volume:
        return QColor(63, 173, 143);
    case NodeGraphValueType::Field:
        return QColor(78, 173, 219);
    case NodeGraphValueType::Vector3:
        return QColor(92, 188, 224);
    case NodeGraphValueType::ScalarFloat:
        return QColor(241, 165, 76);
    case NodeGraphValueType::ScalarInt:
        return QColor(230, 142, 74);
    case NodeGraphValueType::ScalarBool:
        return QColor(235, 214, 97);
    }

    return QColor(152, 162, 180);
}

qreal socketY(std::size_t index) {
    return socketStartY + static_cast<qreal>(index) * socketSpacing;
}

QPointF inputAnchor(const QRectF& rect) {
    return QPointF(rect.left(), rect.center().y());
}

QPointF outputAnchor(const QRectF& rect) {
    return QPointF(rect.right(), rect.center().y());
}

QPainterPath buildEdgePath(
    const QPointF& src,
    const QPointF& dst,
    NodeGraphSocketDirection srcDirection,
    NodeGraphSocketDirection dstDirection) {
    const qreal dx = dst.x() - src.x();
    const qreal baseTangent = std::max(48.0, std::min(240.0, std::fabs(dx) * 0.5));

    const qreal srcSign = (srcDirection == NodeGraphSocketDirection::Output) ? 1.0 : -1.0;
    const qreal dstSign = (dstDirection == NodeGraphSocketDirection::Input) ? -1.0 : 1.0;

    QPainterPath path(src);
    path.cubicTo(
        QPointF(src.x() + srcSign * baseTangent, src.y()),
        QPointF(dst.x() + dstSign * baseTangent, dst.y()),
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

} // namespace nodegraphscene
