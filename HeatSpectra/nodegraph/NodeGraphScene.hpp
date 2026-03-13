#pragma once

#include "NodeGraphBridge.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <QGraphicsScene>
#include <QPointF>
#include <QString>

class QGraphicsEllipseItem;
class QGraphicsSceneMouseEvent;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QPainter;
class QRectF;

class NodeGraphScene : public QGraphicsScene {
public:
    using NodeActivatedCallback = std::function<void(NodeGraphNodeId, const QPointF&)>;
    using NodeSelectionChangedCallback = std::function<void(NodeGraphNodeId)>;
    using StatusCallback = std::function<void(const QString&)>;

    explicit NodeGraphScene(QObject* parent = nullptr);

    void setBridge(NodeGraphBridge* bridge);
    void setNodeActivatedCallback(NodeActivatedCallback callback);
    void setNodeSelectionChangedCallback(NodeSelectionChangedCallback callback);
    void setStatusCallback(StatusCallback callback);
    void refreshFromGraph();
    void setSelectedNode(NodeGraphNodeId nodeId);
    void clearNodeSelection();
    bool copySelectedNodes();
    bool pasteCopiedNodes();
    bool removeSelectedNodes();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    static constexpr int NodeIdRole = 0;
    static constexpr int EdgeIdRole = 1;
    static constexpr int SocketRole = 2;
    static constexpr int SocketIdRole = 3;
    static constexpr int SocketDirectionRole = 4;
    static constexpr int NodeBasePenColorRole = 5;
    static constexpr int EdgeFromSocketRole = 6;
    static constexpr int EdgeToSocketRole = 7;
    static constexpr int EdgeBaseColorRole = 8;
    static constexpr int NodeHoveredRole = 9;

    NodeGraphBridge* bridge = nullptr;
    NodeActivatedCallback nodeActivatedCallback;
    NodeSelectionChangedCallback nodeSelectionChangedCallback;
    StatusCallback statusCallback;
    QGraphicsPathItem* activeDragLine = nullptr;
    QPointF activeDragStartPos{};
    NodeGraphNodeId activeDragFromNode{};
    NodeGraphSocketId activeDragFromSocket{};
    NodeGraphSocketDirection activeDragFromDirection = NodeGraphSocketDirection::Output;
    bool isDraggingConnection = false;
    bool suppressNodeActivationOnRelease = false;
    std::unordered_map<uint32_t, QGraphicsEllipseItem*> inputSocketItemsBySocket;
    std::unordered_map<uint32_t, QGraphicsEllipseItem*> outputSocketItemsBySocket;
    QGraphicsRectItem* hoveredNodeItem = nullptr;
    QGraphicsPathItem* hoveredEdgeItem = nullptr;
    struct CopiedNode {
        NodeGraphNodeId sourceNodeId{};
        NodeTypeId typeId;
        std::string title;
        float x = 0.0f;
        float y = 0.0f;
        std::vector<NodeGraphParamValue> parameters;
        std::vector<NodeGraphSocketId> outputSocketIds;
    };
    struct CopiedEdge {
        NodeGraphNodeId fromNode{};
        NodeGraphSocketId fromSocket{};
        NodeGraphNodeId toNode{};
        NodeGraphSocketId toSocket{};
    };
    std::vector<CopiedNode> copiedNodes;
    std::vector<CopiedEdge> copiedEdges;
    uint32_t pasteGeneration = 0;
    bool suppressSelectionChangedNotifications = false;

    void syncNodePositionsToBridge();
    void clearActiveDragLine();
    void updateEdgePathsFromCurrentLayout();
    void updateHoverState(const QPointF& scenePos);
    void notifySelectedNodeChanged();
    void selectNodesById(const std::vector<NodeGraphNodeId>& nodeIds);
    std::vector<NodeGraphNodeId> selectedTopLevelNodeIds() const;
    NodeGraphNodeId selectedSingleNodeId() const;
    static void setNodeHovered(QGraphicsRectItem* item, bool hovered);
    static void setEdgeHovered(QGraphicsPathItem* item, bool hovered);
    static NodeGraphEdgeId itemEdgeId(const QGraphicsItem* item);
    static bool extractSocketFromItem(const QGraphicsItem* item, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId, NodeGraphSocketDirection& outDirection);
    bool socketAtScenePos(const QPointF& scenePos, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId, NodeGraphSocketDirection& outDirection) const;
    void reportStatus(const QString& text) const;
    static NodeGraphNodeId itemNodeId(const QGraphicsItem* item);
};
