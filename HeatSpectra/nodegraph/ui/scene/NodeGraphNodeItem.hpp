#pragma once

#include "nodegraph/ui/scene/NodeGraphSceneStyle.hpp"
#include "nodegraph/NodeGraphTypes.hpp"

#include <QGraphicsObject>
#include <QPainterPath>

class NodeGraphNodeItem : public QGraphicsObject {
public:
    NodeGraphNodeItem(const NodeGraphNode& node, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    void setDisplayEnabled(bool enabled);
    void setFrozen(bool frozen);
    void setHoveredState(bool hovered);

    bool displayEnabled() const;
    bool frozen() const;
    NodeGraphNodeId nodeId() const;
    nodegraphscene::NodeHitRegion hitRegionAt(const QPointF& localPos) const;
    QPointF inputSocketPosition(std::size_t index, std::size_t total) const;
    QPointF outputSocketPosition(std::size_t index, std::size_t total) const;

protected:
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void drawCap(
        QPainter* painter,
        const QPainterPath& path,
        const QColor& activeColor,
        const QColor& inactiveColor,
        bool active,
        bool hovered) const;

    NodeGraphNodeId m_nodeId{};
    NodeTypeId m_typeId;
    bool m_displayEnabled = false;
    bool m_frozen = false;
    bool m_hovered = false;
    nodegraphscene::NodeHitRegion m_hoverRegion = nodegraphscene::NodeHitRegion::None;
};
