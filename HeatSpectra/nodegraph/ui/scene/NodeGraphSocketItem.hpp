#pragma once

#include "nodegraph/NodeGraphTypes.hpp"

#include <QGraphicsEllipseItem>
#include <QRectF>

class NodeGraphSocketItem final : public QGraphicsEllipseItem {
public:
    NodeGraphSocketItem(
        NodeGraphNodeId nodeId,
        NodeGraphSocketId socketId,
        NodeGraphSocketDirection direction,
        NodeGraphValueType valueType,
        const QRectF& rect,
        QGraphicsItem* parent = nullptr)
        : QGraphicsEllipseItem(rect, parent),
          m_nodeId(nodeId),
          m_socketId(socketId),
          m_direction(direction),
          m_valueType(valueType) {
    }

    NodeGraphNodeId nodeId() const { return m_nodeId; }
    NodeGraphSocketId socketId() const { return m_socketId; }
    NodeGraphSocketDirection direction() const { return m_direction; }
    NodeGraphValueType valueType() const { return m_valueType; }

private:
    NodeGraphNodeId m_nodeId{};
    NodeGraphSocketId m_socketId{};
    NodeGraphSocketDirection m_direction = NodeGraphSocketDirection::Input;
    NodeGraphValueType m_valueType = NodeGraphValueType::None;
};
