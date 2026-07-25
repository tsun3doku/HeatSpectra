#pragma once

#include "NodeGraphUiModel.hpp"

#include <QtQuick/QQuickPaintedItem>
#include <QtGui/QImage>

#include <cstdint>
#include <utility>
#include <vector>

class QHoverEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

class NodeGraphCanvasItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(NodeGraphUiModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    explicit NodeGraphCanvasItem(QQuickItem* parent = nullptr);

    NodeGraphUiModel* model() const;
    void setModel(NodeGraphUiModel* model);

signals:
    void modelChanged();
    void createMenuRequested(qreal graphX, qreal graphY);
    void nodeMenuRequested(int nodeId);

protected:
    void paint(QPainter* painter) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Socket {
        int id = 0;
        int valueType = 0;
        std::vector<int> acceptedValueTypes;
    };

    struct Node {
        int id = 0;
        QString title;
        QString typeId;
        QImage icon;
        qreal x = 0.0;
        qreal y = 0.0;
        std::vector<Socket> inputs;
        std::vector<Socket> outputs;
        bool displayEnabled = false;
        bool frozen = false;
        bool selected = false;
    };

    struct Edge {
        int id = 0;
        int fromNode = 0;
        int fromSocket = 0;
        int toNode = 0;
        int toSocket = 0;
        int valueType = 0;
    };

    enum class Interaction : uint8_t {
        None,
        Pan,
        MoveNode,
        Connect,
        BoxSelect
    };

    void refreshModel();
    void requestRender();
    QPointF graphPosition(const QPointF& itemPosition) const;
    QPointF itemPosition(const QPointF& graphPosition) const;
    int nodeIndexAt(const QPointF& graphPosition) const;
    int socketIndexAt(const Node& node, bool output, const QPointF& itemPosition) const;
    QPointF socketPosition(const Node& node, bool output, int index) const;
    QPointF socketPositionById(const Node& node, bool output, int socketId) const;
    const Node* findNode(int nodeId) const;
    Node* findNode(int nodeId);
    void updateHover(const QPointF& graphPosition);
    static bool socketTypesCompatible(const Socket& source, const Socket& target);
    int edgeIndexAt(const QPointF& graphPosition) const;
    void finishConnection(const QPointF& itemPosition);

    NodeGraphUiModel* graphModel = nullptr;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    qreal zoomValue = 0.54;
    QPointF pan;
    bool panInitialized = false;
    Interaction interaction = Interaction::None;
    QPointF pressItemPosition;
    QPointF lastItemPosition;
    std::vector<std::pair<int, QPointF>> dragSelectionOrigins;
    int interactionNodeId = 0;
    int interactionSocketId = 0;
    bool connectionFromOutput = false;
    QPointF connectionEnd;
    QPointF selectionStart;
    QPointF selectionEnd;
    bool selectionAdditive = false;
    int hoveredNodeId = 0;
    int hoveredCap = 0;
    int hoveredSocketNodeId = 0;
    int hoveredSocketId = 0;
    bool hoveredSocketOutput = false;
    bool hoveredSocketCompatible = true;
    int hoveredEdgeIndex = -1;
};
