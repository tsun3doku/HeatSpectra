#include "NodeGraphScene.hpp"
#include "NodeGraphNodeRectItem.hpp"
#include "NodeGraphSceneStyle.hpp"
#include "NodeGraphSceneUtils.hpp"

#include <QBrush>
#include <QColor>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QPen>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

NodeGraphScene::NodeGraphScene(QObject* parent)
    : QGraphicsScene(parent) {
    setSceneRect(-2000.0, -2000.0, 4000.0, 4000.0);
    connect(this, &QGraphicsScene::selectionChanged, this, [this]() {
        if (suppressSelectionChangedNotifications) {
            return;
        }
        notifySelectedNodeChanged();
    });
}

void NodeGraphScene::setBridge(NodeGraphBridge* bridgePtr) {
    bridge = bridgePtr;
    editor.setBridge(bridgePtr);
    if (!bridge) {
        clearSceneState();
        return;
    }
    const NodeGraphState state = bridge->state();
    lastSeenRevision = state.revision;
    buildFromState(state);
}

void NodeGraphScene::setNodeActivatedCallback(NodeActivatedCallback callback) {
    nodeActivatedCallback = std::move(callback);
}

void NodeGraphScene::setNodeSelectionChangedCallback(NodeSelectionChangedCallback callback) {
    nodeSelectionChangedCallback = std::move(callback);
}

void NodeGraphScene::setStatusCallback(StatusCallback callback) {
    statusCallback = std::move(callback);
}

void NodeGraphScene::applyPendingChanges() {
    if (!bridge) {
        return;
    }

    NodeGraphDelta delta{};
    if (!bridge->consumeChanges(lastSeenRevision, delta)) {
        return;
    }

    applyDelta(delta);
}

void NodeGraphScene::buildFromState(const NodeGraphState& state) {
    const std::vector<NodeGraphNodeId> selectedNodeIds = selectedTopLevelNodeIds();

    suppressSelectionChangedNotifications = true;
    clearSceneState();

    for (const NodeGraphNode& node : state.nodes) {
        createNodeItem(node);
    }
    for (const NodeGraphEdge& edge : state.edges) {
        createEdgeItem(edge);
    }

    selectNodesById(selectedNodeIds);
    suppressSelectionChangedNotifications = false;
    notifySelectedNodeChanged();
}

void NodeGraphScene::applyDelta(const NodeGraphDelta& delta) {
    if (!bridge) {
        return;
    }

    const std::vector<NodeGraphNodeId> selectedNodeIds = selectedTopLevelNodeIds();
    const NodeGraphState state = bridge->state();

    for (const NodeGraphChange& change : delta.changes) {
        switch (change.type) {
        case NodeGraphChangeType::Reset:
            buildFromState(state);
            return;
        case NodeGraphChangeType::NodeUpsert:
            removeNodeItem(change.node.id);
            createNodeItem(change.node);
            for (const NodeGraphEdge& edge : state.edges) {
                if (edge.fromNode == change.node.id || edge.toNode == change.node.id) {
                    createEdgeItem(edge);
                }
            }
            break;
        case NodeGraphChangeType::NodeRemoved:
            removeNodeItem(change.nodeId);
            break;
        case NodeGraphChangeType::EdgeUpsert:
            createEdgeItem(change.edge);
            break;
        case NodeGraphChangeType::EdgeRemoved:
            removeEdgeItem(change.edgeId);
            break;
        }
    }

    updateEdgePathsFromCurrentLayout();
    suppressSelectionChangedNotifications = true;
    selectNodesById(selectedNodeIds);
    suppressSelectionChangedNotifications = false;
    notifySelectedNodeChanged();
}

void NodeGraphScene::clearSceneState() {
    clearActiveDragLine();
    hoveredNodeItem = nullptr;
    hoveredEdgeItem = nullptr;
    inputSocketItemsBySocket.clear();
    outputSocketItemsBySocket.clear();
    outputValueTypeBySocketId.clear();
    nodeItemsById.clear();
    edgeItemsById.clear();
    clear();
}

QGraphicsRectItem* NodeGraphScene::createNodeItem(const NodeGraphNode& node) {
    const QColor headerColor = nodegraphscene::categoryColor(node.category);
    const QColor nodeBorderColor = headerColor.lighter(150);

    QGraphicsRectItem* nodeRect = new NodeGraphNodeRectItem(
        QRectF(0.0, 0.0, nodegraphscene::nodeWidth, nodegraphscene::nodeHeight),
        NodeBasePenColorRole,
        NodeHoveredRole);
    nodeRect->setBrush(QBrush(QColor(32, 38, 56)));
    addItem(nodeRect);
    nodeRect->setPos(node.x, node.y);
    nodeRect->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
    nodeRect->setData(NodeBasePenColorRole, static_cast<qulonglong>(nodeBorderColor.rgba()));
    nodeRect->setData(NodeHoveredRole, false);
    nodeRect->setFlag(QGraphicsItem::ItemIsMovable, true);
    nodeRect->setFlag(QGraphicsItem::ItemIsSelectable, true);
    nodeRect->setZValue(1.0);

    QGraphicsRectItem* headerRect = new QGraphicsRectItem(0.0, 0.0, nodegraphscene::nodeWidth, nodegraphscene::nodeHeaderHeight, nodeRect);
    headerRect->setPen(QPen(Qt::NoPen));
    headerRect->setBrush(headerColor);
    nodegraphscene::setDecorativeItemFlags(headerRect);

    QGraphicsSimpleTextItem* titleItem = new QGraphicsSimpleTextItem(QString::fromStdString(node.title), nodeRect);
    titleItem->setBrush(QColor(244, 244, 244));
    titleItem->setPos(10.0, 8.0);
    nodegraphscene::setDecorativeItemFlags(titleItem);

    for (std::size_t index = 0; index < node.inputs.size(); ++index) {
        const NodeGraphSocket& socket = node.inputs[index];
        const qreal y = nodegraphscene::socketY(index);

        QGraphicsSimpleTextItem* inputItem = new QGraphicsSimpleTextItem(QString::fromStdString(socket.name), nodeRect);
        inputItem->setBrush(QColor(180, 190, 210));
        inputItem->setPos(12.0, y - 8.0);
        nodegraphscene::setDecorativeItemFlags(inputItem);

        QGraphicsEllipseItem* inputSocket = new QGraphicsEllipseItem(-nodegraphscene::socketRadius, y - nodegraphscene::socketRadius, nodegraphscene::socketRadius * 2.0, nodegraphscene::socketRadius * 2.0, nodeRect);
        inputSocket->setPen(QPen(QColor(24, 28, 38), 1.0));
        inputSocket->setBrush(nodegraphscene::valueTypeColor(socket.valueType));
        inputSocket->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
        inputSocket->setData(SocketRole, true);
        inputSocket->setData(SocketIdRole, static_cast<qulonglong>(socket.id.value));
        inputSocket->setData(SocketDirectionRole, static_cast<int>(NodeGraphSocketDirection::Input));
        nodegraphscene::setDecorativeItemFlags(inputSocket);

        inputSocketItemsBySocket[socket.id.value] = inputSocket;
    }

    for (std::size_t index = 0; index < node.outputs.size(); ++index) {
        const NodeGraphSocket& socket = node.outputs[index];
        const qreal y = nodegraphscene::socketY(index);

        QGraphicsSimpleTextItem* outputItem = new QGraphicsSimpleTextItem(QString::fromStdString(socket.name), nodeRect);
        outputItem->setBrush(QColor(180, 210, 190));
        outputItem->setPos(nodegraphscene::nodeWidth - outputItem->boundingRect().width() - 12.0, y - 8.0);
        nodegraphscene::setDecorativeItemFlags(outputItem);

        QGraphicsEllipseItem* outputSocket = new QGraphicsEllipseItem(nodegraphscene::nodeWidth - nodegraphscene::socketRadius, y - nodegraphscene::socketRadius, nodegraphscene::socketRadius * 2.0, nodegraphscene::socketRadius * 2.0, nodeRect);
        outputSocket->setPen(QPen(QColor(24, 28, 38), 1.0));
        outputSocket->setBrush(nodegraphscene::valueTypeColor(socket.valueType));
        outputSocket->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
        outputSocket->setData(SocketRole, true);
        outputSocket->setData(SocketIdRole, static_cast<qulonglong>(socket.id.value));
        outputSocket->setData(SocketDirectionRole, static_cast<int>(NodeGraphSocketDirection::Output));
        nodegraphscene::setDecorativeItemFlags(outputSocket);

        outputValueTypeBySocketId[socket.id.value] = socket.valueType;
        outputSocketItemsBySocket[socket.id.value] = outputSocket;
    }

    nodeItemsById[node.id.value] = nodeRect;
    return nodeRect;
}

void NodeGraphScene::removeEdgesForNode(NodeGraphNodeId nodeId) {
    std::vector<uint32_t> edgeIdsToRemove;
    edgeIdsToRemove.reserve(edgeItemsById.size());
    for (const auto& entry : edgeItemsById) {
        QGraphicsPathItem* edgeItem = entry.second;
        if (!edgeItem) {
            continue;
        }

        bool srcOk = false;
        bool dstOk = false;
        const uint32_t srcSocketValue = static_cast<uint32_t>(edgeItem->data(EdgeFromSocketRole).toULongLong(&srcOk));
        const uint32_t dstSocketValue = static_cast<uint32_t>(edgeItem->data(EdgeToSocketRole).toULongLong(&dstOk));
        if (!srcOk || !dstOk) {
            continue;
        }

        const auto srcSocketIt = outputSocketItemsBySocket.find(srcSocketValue);
        if (srcSocketIt != outputSocketItemsBySocket.end()) {
            const QVariant nodeIdData = srcSocketIt->second->data(NodeIdRole);
            if (nodeIdData.isValid() && nodeIdData.toULongLong() == nodeId.value) {
                edgeIdsToRemove.push_back(entry.first);
                continue;
            }
        }

        const auto dstSocketIt = inputSocketItemsBySocket.find(dstSocketValue);
        if (dstSocketIt != inputSocketItemsBySocket.end()) {
            const QVariant nodeIdData = dstSocketIt->second->data(NodeIdRole);
            if (nodeIdData.isValid() && nodeIdData.toULongLong() == nodeId.value) {
                edgeIdsToRemove.push_back(entry.first);
            }
        }
    }

    for (uint32_t edgeId : edgeIdsToRemove) {
        removeEdgeItem(NodeGraphEdgeId{edgeId});
    }
}

void NodeGraphScene::removeNodeItem(NodeGraphNodeId nodeId) {
    if (!nodeId.isValid()) {
        return;
    }

    auto it = nodeItemsById.find(nodeId.value);
    if (it == nodeItemsById.end()) {
        return;
    }

    removeEdgesForNode(nodeId);

    QGraphicsRectItem* nodeRect = it->second;
    if (nodeRect) {
        const QList<QGraphicsItem*> children = nodeRect->childItems();
        for (QGraphicsItem* child : children) {
            if (!child || !child->data(SocketRole).isValid()) {
                continue;
            }
            bool socketOk = false;
            const uint32_t socketId = static_cast<uint32_t>(child->data(SocketIdRole).toULongLong(&socketOk));
            if (!socketOk) {
                continue;
            }
            const int directionValue = child->data(SocketDirectionRole).toInt();
            if (directionValue == static_cast<int>(NodeGraphSocketDirection::Input)) {
                inputSocketItemsBySocket.erase(socketId);
            } else {
                outputSocketItemsBySocket.erase(socketId);
                outputValueTypeBySocketId.erase(socketId);
            }
        }
        removeItem(nodeRect);
        delete nodeRect;
    }

    nodeItemsById.erase(it);
}

QGraphicsPathItem* NodeGraphScene::createEdgeItem(const NodeGraphEdge& edge) {
    if (edge.id.isValid()) {
        removeEdgeItem(edge.id);
    }

    QPointF src{};
    QPointF dst{};

    const auto srcSocketIt = outputSocketItemsBySocket.find(edge.fromSocket.value);
    const auto dstSocketIt = inputSocketItemsBySocket.find(edge.toSocket.value);
    if (srcSocketIt != outputSocketItemsBySocket.end()) {
        src = srcSocketIt->second->sceneBoundingRect().center();
    }
    if (dstSocketIt != inputSocketItemsBySocket.end()) {
        dst = dstSocketIt->second->sceneBoundingRect().center();
    }

    if (srcSocketIt == outputSocketItemsBySocket.end() || dstSocketIt == inputSocketItemsBySocket.end()) {
        const auto srcNodeIt = nodeItemsById.find(edge.fromNode.value);
        const auto dstNodeIt = nodeItemsById.find(edge.toNode.value);
        if (srcNodeIt == nodeItemsById.end() || dstNodeIt == nodeItemsById.end()) {
            return nullptr;
        }

        src = nodegraphscene::outputAnchor(srcNodeIt->second->sceneBoundingRect());
        dst = nodegraphscene::inputAnchor(dstNodeIt->second->sceneBoundingRect());
    }

    QColor edgeColor(120, 200, 255);
    const auto valueTypeIt = outputValueTypeBySocketId.find(edge.fromSocket.value);
    if (valueTypeIt != outputValueTypeBySocketId.end()) {
        edgeColor = nodegraphscene::valueTypeColor(valueTypeIt->second).lighter(135);
    }

    QPen edgePen(edgeColor, 2.2);
    edgePen.setCapStyle(Qt::RoundCap);
    edgePen.setJoinStyle(Qt::RoundJoin);
    QGraphicsPathItem* line = addPath(nodegraphscene::buildEdgePath(src, dst), edgePen);
    line->setZValue(-1.0);
    line->setData(EdgeIdRole, static_cast<qulonglong>(edge.id.value));
    line->setData(EdgeFromSocketRole, static_cast<qulonglong>(edge.fromSocket.value));
    line->setData(EdgeToSocketRole, static_cast<qulonglong>(edge.toSocket.value));
    line->setData(EdgeBaseColorRole, static_cast<qulonglong>(edgeColor.rgba()));

    edgeItemsById[edge.id.value] = line;
    return line;
}

void NodeGraphScene::removeEdgeItem(NodeGraphEdgeId edgeId) {
    if (!edgeId.isValid()) {
        return;
    }
    auto it = edgeItemsById.find(edgeId.value);
    if (it == edgeItemsById.end()) {
        return;
    }
    QGraphicsPathItem* edgeItem = it->second;
    if (edgeItem) {
        removeItem(edgeItem);
        delete edgeItem;
    }
    edgeItemsById.erase(it);
}

void NodeGraphScene::setSelectedNode(NodeGraphNodeId nodeId) {
    suppressSelectionChangedNotifications = true;
    clearSelection();
    if (nodeId.isValid()) {
        selectNodesById({nodeId});
    }
    suppressSelectionChangedNotifications = false;
    notifySelectedNodeChanged();
}

void NodeGraphScene::clearNodeSelection() {
    setSelectedNode({});
}

bool NodeGraphScene::copySelectedNodes() {
    copiedNodes.clear();
    copiedEdges.clear();
    pasteGeneration = 0;

    if (!bridge) {
        return false;
    }

    std::unordered_set<uint32_t> selectedNodeIds;
    for (QGraphicsItem* item : selectedItems()) {
        if (!item || item->parentItem() != nullptr) {
            continue;
        }
        if (!item->data(NodeIdRole).isValid()) {
            continue;
        }
        if (item->data(SocketRole).isValid() && item->data(SocketRole).toBool()) {
            continue;
        }

        const NodeGraphNodeId nodeId = itemNodeId(item);
        if (nodeId.isValid()) {
            selectedNodeIds.insert(nodeId.value);
        }
    }

    if (selectedNodeIds.empty()) {
        return false;
    }

    const NodeGraphState state = bridge->state();
    for (const NodeGraphNode& node : state.nodes) {
        if (selectedNodeIds.find(node.id.value) == selectedNodeIds.end()) {
            continue;
        }

        NodeGraphEditor::CopiedNode copiedNode{};
        copiedNode.sourceNodeId = node.id;
        copiedNode.typeId = node.typeId;
        copiedNode.title = node.title;
        copiedNode.x = node.x;
        copiedNode.y = node.y;
        copiedNode.parameters = node.parameters;
        copiedNode.outputSocketIds.reserve(node.outputs.size());
        for (const NodeGraphSocket& socket : node.outputs) {
            copiedNode.outputSocketIds.push_back(socket.id);
        }
        copiedNodes.push_back(std::move(copiedNode));
    }

    if (copiedNodes.empty()) {
        return false;
    }

    std::sort(
        copiedNodes.begin(),
        copiedNodes.end(),
        [](const NodeGraphEditor::CopiedNode& lhs, const NodeGraphEditor::CopiedNode& rhs) {
            return lhs.sourceNodeId.value < rhs.sourceNodeId.value;
        });

    for (const NodeGraphEdge& edge : state.edges) {
        if (selectedNodeIds.find(edge.fromNode.value) == selectedNodeIds.end() ||
            selectedNodeIds.find(edge.toNode.value) == selectedNodeIds.end()) {
            continue;
        }

        copiedEdges.push_back({edge.fromNode, edge.fromSocket, edge.toNode, edge.toSocket});
    }

    return true;
}

bool NodeGraphScene::pasteCopiedNodes() {
    if (!bridge || copiedNodes.empty()) {
        return false;
    }

    ++pasteGeneration;
    const float positionOffset = 40.0f * static_cast<float>(pasteGeneration);
    std::vector<NodeGraphNodeId> createdNodeIds;
    if (!editor.pasteCopiedNodes(copiedNodes, copiedEdges, positionOffset, createdNodeIds)) {
        return false;
    }

    applyPendingChanges();
    selectNodesById(createdNodeIds);
    return true;
}

bool NodeGraphScene::removeSelectedNodes() {
    if (!bridge) {
        return false;
    }

    bool removedAny = false;
    for (QGraphicsItem* item : selectedItems()) {
        if (!item || !item->data(NodeIdRole).isValid()) {
            continue;
        }

        const NodeGraphNodeId nodeId = itemNodeId(item);
        removedAny = editor.removeNode(nodeId) || removedAny;
    }

    if (removedAny) {
        applyPendingChanges();
    }

    return removedAny;
}

void NodeGraphScene::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsScene::drawBackground(painter, rect);

    painter->save();
    const auto& clearColor = nodegraphscene::clearColor();
    painter->fillRect(rect, QColor::fromRgbF(clearColor[0], clearColor[1], clearColor[2], clearColor[3]));

    painter->restore();
}

void NodeGraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!event || event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if ((event->modifiers() & Qt::ControlModifier) && bridge) {
        const NodeGraphEdgeId edgeId = itemEdgeId(itemAt(event->scenePos(), QTransform()));
        if (edgeId.isValid()) {
            if (editor.removeConnection(edgeId)) {
                reportStatus("Connection removed.");
                applyPendingChanges();
            } else {
                reportStatus("Failed to remove connection.");
            }
            event->accept();
            return;
        }
    }

    NodeGraphNodeId nodeId{};
    NodeGraphSocketId socketId{};
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    if (socketAtScenePos(event->scenePos(), nodeId, socketId, direction)) {
        if (direction == NodeGraphSocketDirection::Input && bridge) {
            if (editor.disconnectIncomingInput(nodeId, socketId)) {
                reportStatus("Input disconnected. Drop on an output socket to reconnect.");
                applyPendingChanges();
            }
        }

        isDraggingConnection = true;
        suppressNodeActivationOnRelease = true;
        activeDragFromNode = nodeId;
        activeDragFromSocket = socketId;
        activeDragFromDirection = direction;

        clearSelection();
        clearActiveDragLine();
        activeDragStartPos = event->scenePos();
        if (activeDragFromDirection == NodeGraphSocketDirection::Output) {
            const auto socketIt = outputSocketItemsBySocket.find(activeDragFromSocket.value);
            if (socketIt != outputSocketItemsBySocket.end() && socketIt->second) {
                activeDragStartPos = socketIt->second->mapToScene(socketIt->second->boundingRect().center());
            }
        } else {
            const auto socketIt = inputSocketItemsBySocket.find(activeDragFromSocket.value);
            if (socketIt != inputSocketItemsBySocket.end() && socketIt->second) {
                activeDragStartPos = socketIt->second->mapToScene(socketIt->second->boundingRect().center());
            }
        }
        QPen previewPen(QColor(166, 206, 255), 2.2, Qt::DashLine, Qt::RoundCap);
        previewPen.setJoinStyle(Qt::RoundJoin);
        const NodeGraphSocketDirection previewEndDirection =
            (activeDragFromDirection == NodeGraphSocketDirection::Output) ? NodeGraphSocketDirection::Input : NodeGraphSocketDirection::Output;
        activeDragLine = addPath(nodegraphscene::buildEdgePath(activeDragStartPos, event->scenePos(), activeDragFromDirection, previewEndDirection), previewPen);
        activeDragLine->setZValue(3.0);
        event->accept();
        return;
    }

    QGraphicsScene::mousePressEvent(event);
}

void NodeGraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (isDraggingConnection && activeDragLine && event) {
        const NodeGraphSocketDirection previewEndDirection =
            (activeDragFromDirection == NodeGraphSocketDirection::Output) ? NodeGraphSocketDirection::Input : NodeGraphSocketDirection::Output;
        activeDragLine->setPath(nodegraphscene::buildEdgePath(activeDragStartPos, event->scenePos(), activeDragFromDirection, previewEndDirection));
        updateHoverState(event->scenePos());
        event->accept();
        return;
    }

    QGraphicsScene::mouseMoveEvent(event);
    updateEdgePathsFromCurrentLayout();
    if (event) {
        updateHoverState(event->scenePos());
    }
}

void NodeGraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (isDraggingConnection && event && event->button() == Qt::LeftButton) {
        NodeGraphNodeId toNode{};
        NodeGraphSocketId toSocket{};
        NodeGraphSocketDirection toDirection = NodeGraphSocketDirection::Output;

        if (bridge && socketAtScenePos(event->scenePos(), toNode, toSocket, toDirection)) {
            if (toDirection == activeDragFromDirection) {
                reportStatus("Connect an output socket to an input socket.");
            } else {
                NodeGraphNodeId fromNode{};
                NodeGraphSocketId fromSocket{};
                NodeGraphNodeId targetNode{};
                NodeGraphSocketId targetSocket{};

                if (activeDragFromDirection == NodeGraphSocketDirection::Output) {
                    fromNode = activeDragFromNode;
                    fromSocket = activeDragFromSocket;
                    targetNode = toNode;
                    targetSocket = toSocket;
                } else {
                    fromNode = toNode;
                    fromSocket = toSocket;
                    targetNode = activeDragFromNode;
                    targetSocket = activeDragFromSocket;
                }

                std::string errorMessage;
                if (editor.connectSockets(fromNode, fromSocket, targetNode, targetSocket, errorMessage)) {
                    reportStatus("Sockets connected.");
                    applyPendingChanges();
                } else {
                    reportStatus(QString::fromStdString(errorMessage.empty() ? "Failed to connect sockets." : errorMessage));
                }
            }
        } else {
            clearActiveDragLine();
        }

        clearActiveDragLine();
        isDraggingConnection = false;
        activeDragStartPos = {};
        activeDragFromNode = {};
        activeDragFromSocket = {};
        activeDragFromDirection = NodeGraphSocketDirection::Output;
        suppressNodeActivationOnRelease = false;
        event->accept();
        updateHoverState(event->scenePos());
        return;
    }

    if (event && event->button() == Qt::LeftButton) {
        const qreal dragDistance = QLineF(event->buttonDownScenePos(Qt::LeftButton), event->scenePos()).length();
        if (dragDistance < 3.0 && nodeActivatedCallback && !suppressNodeActivationOnRelease) {
            QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
            const NodeGraphNodeId nodeId = itemNodeId(item);
            if (nodeId.isValid()) {
                nodeActivatedCallback(nodeId, event->scenePos());
            }
        }

        suppressNodeActivationOnRelease = false;
    }

    QGraphicsScene::mouseReleaseEvent(event);
    syncNodePositionsToBridge();
    if (event) {
        updateHoverState(event->scenePos());
    }
}

void NodeGraphScene::syncNodePositionsToBridge() {
    if (!bridge) {
        return;
    }

    bool changed = false;
    const QList<QGraphicsItem*> allItems = items();
    for (QGraphicsItem* item : allItems) {
        if (!item || !item->data(NodeIdRole).isValid()) {
            continue;
        }
        if (item->parentItem() != nullptr) {
            continue;
        }
        if (!item->flags().testFlag(QGraphicsItem::ItemIsMovable)) {
            continue;
        }
        if (item->data(SocketRole).isValid() && item->data(SocketRole).toBool()) {
            continue;
        }

        const NodeGraphNodeId nodeId = itemNodeId(item);
        const QPointF position = item->scenePos();
        changed = editor.moveNode(nodeId, static_cast<float>(position.x()), static_cast<float>(position.y())) || changed;
    }

    if (changed) {
        applyPendingChanges();
    }
}

void NodeGraphScene::updateEdgePathsFromCurrentLayout() {
    const QList<QGraphicsItem*> allItems = items();
    for (QGraphicsItem* item : allItems) {
        if (!item || !item->data(EdgeIdRole).isValid()) {
            continue;
        }

        bool srcOk = false;
        bool dstOk = false;
        const uint32_t srcSocketValue = static_cast<uint32_t>(item->data(EdgeFromSocketRole).toULongLong(&srcOk));
        const uint32_t dstSocketValue = static_cast<uint32_t>(item->data(EdgeToSocketRole).toULongLong(&dstOk));
        if (!srcOk || !dstOk) {
            continue;
        }

        const auto srcIt = outputSocketItemsBySocket.find(srcSocketValue);
        const auto dstIt = inputSocketItemsBySocket.find(dstSocketValue);
        if (srcIt == outputSocketItemsBySocket.end() || dstIt == inputSocketItemsBySocket.end() || !srcIt->second || !dstIt->second) {
            continue;
        }

        QGraphicsPathItem* edgeItem = dynamic_cast<QGraphicsPathItem*>(item);
        if (!edgeItem) {
            continue;
        }

        const QPointF src = srcIt->second->mapToScene(srcIt->second->boundingRect().center());
        const QPointF dst = dstIt->second->mapToScene(dstIt->second->boundingRect().center());
        edgeItem->setPath(nodegraphscene::buildEdgePath(src, dst));
    }
}

void NodeGraphScene::updateHoverState(const QPointF& scenePos) {
    QGraphicsPathItem* nextHoveredEdge = nullptr;
    QGraphicsRectItem* nextHoveredNode = nullptr;

    const QList<QGraphicsItem*> hitItems = items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
    for (QGraphicsItem* hitItem : hitItems) {
        QGraphicsItem* current = hitItem;
        while (current) {
            if (!nextHoveredEdge && current->data(EdgeIdRole).isValid()) {
                nextHoveredEdge = dynamic_cast<QGraphicsPathItem*>(current);
            }

            if (!nextHoveredNode && current->data(NodeIdRole).isValid() &&
                (!current->data(SocketRole).isValid() || !current->data(SocketRole).toBool())) {
                nextHoveredNode = dynamic_cast<QGraphicsRectItem*>(current);
            }

            current = current->parentItem();
        }

        if (nextHoveredEdge && nextHoveredNode) {
            break;
        }
    }

    if (hoveredEdgeItem != nextHoveredEdge) {
        setEdgeHovered(hoveredEdgeItem, false);
        hoveredEdgeItem = nextHoveredEdge;
        setEdgeHovered(hoveredEdgeItem, true);
    }

    if (hoveredNodeItem != nextHoveredNode) {
        setNodeHovered(hoveredNodeItem, false);
        hoveredNodeItem = nextHoveredNode;
        setNodeHovered(hoveredNodeItem, true);
    }
}

void NodeGraphScene::notifySelectedNodeChanged() {
    if (nodeSelectionChangedCallback) {
        nodeSelectionChangedCallback(selectedSingleNodeId());
    }
}

void NodeGraphScene::selectNodesById(const std::vector<NodeGraphNodeId>& nodeIds) {
    if (nodeIds.empty()) {
        return;
    }

    std::unordered_set<uint32_t> nodeIdSet;
    nodeIdSet.reserve(nodeIds.size());
    for (const NodeGraphNodeId nodeId : nodeIds) {
        if (nodeId.isValid()) {
            nodeIdSet.insert(nodeId.value);
        }
    }

    if (nodeIdSet.empty()) {
        return;
    }

    for (QGraphicsItem* item : items()) {
        if (!item || item->parentItem() != nullptr) {
            continue;
        }
        const NodeGraphNodeId nodeId = itemNodeId(item);
        if (nodeId.isValid() && nodeIdSet.find(nodeId.value) != nodeIdSet.end()) {
            item->setSelected(true);
        }
    }
}

std::vector<NodeGraphNodeId> NodeGraphScene::selectedTopLevelNodeIds() const {
    std::vector<NodeGraphNodeId> nodeIds;
    const QList<QGraphicsItem*> selection = selectedItems();
    nodeIds.reserve(selection.size());

    for (QGraphicsItem* item : selection) {
        if (!item || item->parentItem() != nullptr) {
            continue;
        }
        if (item->data(SocketRole).isValid() && item->data(SocketRole).toBool()) {
            continue;
        }

        const NodeGraphNodeId nodeId = itemNodeId(item);
        if (nodeId.isValid()) {
            nodeIds.push_back(nodeId);
        }
    }

    return nodeIds;
}

NodeGraphNodeId NodeGraphScene::selectedSingleNodeId() const {
    const std::vector<NodeGraphNodeId> nodeIds = selectedTopLevelNodeIds();
    if (nodeIds.size() != 1) {
        return {};
    }
    return nodeIds.front();
}

void NodeGraphScene::setNodeHovered(QGraphicsRectItem* item, bool hovered) {
    if (!item) {
        return;
    }

    item->setData(NodeHoveredRole, hovered);
    item->update();
    item->setZValue(hovered ? 1.1 : 1.0);
}

void NodeGraphScene::setEdgeHovered(QGraphicsPathItem* item, bool hovered) {
    if (!item) {
        return;
    }

    bool ok = false;
    const uint32_t rgba = static_cast<uint32_t>(item->data(EdgeBaseColorRole).toULongLong(&ok));
    const QColor baseColor = ok ? QColor::fromRgba(rgba) : QColor(120, 200, 255);
    QPen pen(hovered ? baseColor.lighter(145) : baseColor, hovered ? 2.8 : 2.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    item->setPen(pen);
}

void NodeGraphScene::clearActiveDragLine() {
    if (!activeDragLine) {
        return;
    }

    removeItem(activeDragLine);
    delete activeDragLine;
    activeDragLine = nullptr;
}

NodeGraphEdgeId NodeGraphScene::itemEdgeId(const QGraphicsItem* item) {
    const QGraphicsItem* current = item;
    while (current) {
        const QVariant idVariant = current->data(EdgeIdRole);
        if (idVariant.isValid()) {
            bool ok = false;
            const uint32_t id = static_cast<uint32_t>(idVariant.toULongLong(&ok));
            if (ok) {
                return NodeGraphEdgeId{id};
            }
        }

        current = current->parentItem();
    }

    return {};
}

bool NodeGraphScene::extractSocketFromItem(const QGraphicsItem* item, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId, NodeGraphSocketDirection& outDirection) {
    const QGraphicsItem* current = item;
    while (current) {
        const QVariant socketMarker = current->data(SocketRole);
        if (socketMarker.isValid() && socketMarker.toBool()) {
            bool nodeOk = false;
            bool socketOk = false;
            const uint32_t nodeValue = static_cast<uint32_t>(current->data(NodeIdRole).toULongLong(&nodeOk));
            const uint32_t socketValue = static_cast<uint32_t>(current->data(SocketIdRole).toULongLong(&socketOk));
            const int directionValue = current->data(SocketDirectionRole).toInt();
            if (nodeOk && socketOk) {
                outNodeId = NodeGraphNodeId{nodeValue};
                outSocketId = NodeGraphSocketId{socketValue};
                outDirection = (directionValue == static_cast<int>(NodeGraphSocketDirection::Output))
                    ? NodeGraphSocketDirection::Output
                    : NodeGraphSocketDirection::Input;
                return true;
            }
        }

        current = current->parentItem();
    }

    return false;
}

bool NodeGraphScene::socketAtScenePos(const QPointF& scenePos, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId, NodeGraphSocketDirection& outDirection) const {
    constexpr qreal socketHitPadding = 8.0;
    const QRectF hitRect(
        scenePos.x() - socketHitPadding,
        scenePos.y() - socketHitPadding,
        socketHitPadding * 2.0,
        socketHitPadding * 2.0);
    const QList<QGraphicsItem*> hitItems = items(hitRect, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
    for (const QGraphicsItem* hitItem : hitItems) {
        if (extractSocketFromItem(hitItem, outNodeId, outSocketId, outDirection)) {
            return true;
        }
    }

    return false;
}

void NodeGraphScene::reportStatus(const QString& text) const {
    if (statusCallback) {
        statusCallback(text);
    }
}

NodeGraphNodeId NodeGraphScene::itemNodeId(const QGraphicsItem* item) {
    const QGraphicsItem* current = item;
    while (current) {
        const QVariant idVariant = current->data(NodeIdRole);
        if (idVariant.isValid()) {
            bool ok = false;
            const uint32_t id = static_cast<uint32_t>(idVariant.toULongLong(&ok));
            if (ok) {
                return NodeGraphNodeId{id};
            }
        }

        current = current->parentItem();
    }

    return {};
}
