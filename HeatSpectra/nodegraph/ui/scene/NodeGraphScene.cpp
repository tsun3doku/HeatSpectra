#include "NodeGraphScene.hpp"
#include "NodeGraphNodeItem.hpp"
#include "NodeGraphSceneStyle.hpp"
#include "NodeGraphSceneUtils.hpp"
#include "NodeGraphSocketItem.hpp"

#include <QBrush>
#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
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
        case NodeGraphChangeType::NodeUpsert: {
            if (change.reason == NodeGraphChangeReason::State) {
                const auto itemIt = nodeItemsById.find(change.node.id.value);
                if (itemIt != nodeItemsById.end() && itemIt->second) {
                    itemIt->second->setDisplayEnabled(change.node.displayEnabled);
                    itemIt->second->setFrozen(change.node.frozen);
                    break;
                }
            }

            removeNodeItem(change.node.id);
            createNodeItem(change.node);
            for (const NodeGraphEdge& edge : state.edges) {
                if (edge.fromNode == change.node.id || edge.toNode == change.node.id) {
                    createEdgeItem(edge);
                }
            }
            break;
        }
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
    hoveredNodeId = {};
    hoveredEdgeId = {};
    hoveredSocketId = {};
    inputSocketItemsBySocket.clear();
    outputSocketItemsBySocket.clear();
    nodeItemsById.clear();
    edgeItemsById.clear();
    clear();
}

NodeGraphNodeItem* NodeGraphScene::createNodeItem(const NodeGraphNode& node) {
    NodeGraphNodeItem* nodeItem = new NodeGraphNodeItem(node);
    addItem(nodeItem);
    nodeItem->setPos(node.x, node.y);
    nodeItem->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable, true);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
    nodeItem->setZValue(1.0);

    QGraphicsSimpleTextItem* titleItem = new QGraphicsSimpleTextItem(QString::fromStdString(node.title), nodeItem);
    titleItem->setBrush(nodegraphscene::titleColor());
    QFont titleFont = titleItem->font();
    titleFont.setPixelSize(static_cast<int>(nodegraphscene::titleFontPixelSize));
    titleFont.setWeight(QFont::Medium);
    titleFont.setHintingPreference(QFont::PreferFullHinting);
    titleItem->setFont(titleFont);
    const QRectF titleBounds = titleItem->boundingRect();
    titleItem->setPos(nodegraphscene::titlePosition(titleBounds));
    nodegraphscene::setDecorativeItemFlags(titleItem);

    for (std::size_t index = 0; index < node.inputs.size(); ++index) {
        const NodeGraphSocket& socket = node.inputs[index];
        const QPointF socketPos = nodeItem->inputSocketPosition(index, node.inputs.size());
        NodeGraphSocketItem* inputSocket = new NodeGraphSocketItem(
            node.id,
            socket.id,
            NodeGraphSocketDirection::Input,
            socket.valueType,
            QRectF(
                socketPos.x() - nodegraphscene::socketRadius,
                socketPos.y() - nodegraphscene::socketRadius,
                nodegraphscene::socketRadius * 2.0,
                nodegraphscene::socketRadius * 2.0),
            nodeItem);
        inputSocket->setPen(QPen(nodegraphscene::socketBorderColor(), nodegraphscene::socketBorderWidth));
        inputSocket->setBrush(nodegraphscene::valueTypeColor(socket.valueType));
        nodegraphscene::setDecorativeItemFlags(inputSocket);
        inputSocketItemsBySocket[socket.id.value] = inputSocket;
    }

    for (std::size_t index = 0; index < node.outputs.size(); ++index) {
        const NodeGraphSocket& socket = node.outputs[index];
        const QPointF socketPos = nodeItem->outputSocketPosition(index, node.outputs.size());
        NodeGraphSocketItem* outputSocket = new NodeGraphSocketItem(
            node.id,
            socket.id,
            NodeGraphSocketDirection::Output,
            socket.valueType,
            QRectF(
                socketPos.x() - nodegraphscene::socketRadius,
                socketPos.y() - nodegraphscene::socketRadius,
                nodegraphscene::socketRadius * 2.0,
                nodegraphscene::socketRadius * 2.0),
            nodeItem);
        outputSocket->setPen(QPen(nodegraphscene::socketBorderColor(), nodegraphscene::socketBorderWidth));
        outputSocket->setBrush(nodegraphscene::valueTypeColor(socket.valueType));
        nodegraphscene::setDecorativeItemFlags(outputSocket);

        outputSocketItemsBySocket[socket.id.value] = outputSocket;
    }

    nodeItemsById[node.id.value] = nodeItem;
    return nodeItem;
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

    NodeGraphNodeItem* nodeItem = it->second;
    if (nodeItem) {
        const QList<QGraphicsItem*> children = nodeItem->childItems();
        for (QGraphicsItem* child : children) {
            NodeGraphSocketItem* socketItem = dynamic_cast<NodeGraphSocketItem*>(child);
            if (!socketItem) {
                continue;
            }
            if (socketItem->direction() == NodeGraphSocketDirection::Input) {
                inputSocketItemsBySocket.erase(socketItem->socketId().value);
            } else {
                outputSocketItemsBySocket.erase(socketItem->socketId().value);
            }
        }
        removeItem(nodeItem);
        delete nodeItem;
    }

    if (hoveredNodeId == nodeId) {
        hoveredNodeId = {};
    }
    if (hoveredSocketId.isValid()) {
        const bool hoveredSocketBelongsToNode =
            inputSocketItemsBySocket.find(hoveredSocketId.value) == inputSocketItemsBySocket.end() &&
            outputSocketItemsBySocket.find(hoveredSocketId.value) == outputSocketItemsBySocket.end();
        if (hoveredSocketBelongsToNode) {
            hoveredSocketId = {};
        }
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

    QColor edgeColor = nodegraphscene::edgeDefaultColor();
    if (srcSocketIt != outputSocketItemsBySocket.end() && srcSocketIt->second) {
        edgeColor = nodegraphscene::valueTypeColor(srcSocketIt->second->valueType()).lighter(135);
    }

    QPen edgePen(edgeColor, nodegraphscene::edgeDefaultWidth);
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
    if (hoveredEdgeId == edgeId) {
        hoveredEdgeId = {};
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
    const float positionOffset = static_cast<float>(nodegraphscene::pasteOffset) * static_cast<float>(pasteGeneration);
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
    painter->fillRect(rect, nodegraphscene::sceneBackgroundColor);

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

    if (handleNodeCapClick(event->scenePos())) {
        event->accept();
        return;
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
        QColor previewColor = nodegraphscene::edgeDefaultColor();
        if (activeDragFromDirection == NodeGraphSocketDirection::Output) {
            const auto socketIt = outputSocketItemsBySocket.find(activeDragFromSocket.value);
            if (socketIt != outputSocketItemsBySocket.end() && socketIt->second) {
                activeDragStartPos = socketIt->second->mapToScene(socketIt->second->boundingRect().center());
                previewColor = nodegraphscene::valueTypeColor(socketIt->second->valueType()).lighter(135);
            }
        } else {
            const auto socketIt = inputSocketItemsBySocket.find(activeDragFromSocket.value);
            if (socketIt != inputSocketItemsBySocket.end() && socketIt->second) {
                activeDragStartPos = socketIt->second->mapToScene(socketIt->second->boundingRect().center());
                previewColor = nodegraphscene::valueTypeColor(socketIt->second->valueType()).lighter(135);
            }
        }
        QPen previewPen(previewColor, nodegraphscene::edgeDefaultWidth, Qt::DashLine, Qt::RoundCap);
        previewPen.setJoinStyle(Qt::RoundJoin);
        const NodeGraphSocketDirection previewEndDirection =
            (activeDragFromDirection == NodeGraphSocketDirection::Output) ? NodeGraphSocketDirection::Input : NodeGraphSocketDirection::Output;
        activeDragLine = addPath(nodegraphscene::buildEdgePath(activeDragStartPos, event->scenePos(), activeDragFromDirection, previewEndDirection), previewPen);
        activeDragLine->setZValue(nodegraphscene::dragLineZValue);
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
        if (dragDistance < nodegraphscene::clickDragThreshold && nodeActivatedCallback && !suppressNodeActivationOnRelease) {
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
    NodeGraphEdgeId nextHoveredEdgeId{};
    NodeGraphNodeId nextHoveredNodeId{};
    NodeGraphSocketId nextHoveredSocketId{};

    const QList<QGraphicsItem*> hitItems = items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
    for (QGraphicsItem* hitItem : hitItems) {
        NodeGraphNodeId hitNodeId{};
        NodeGraphSocketId hitSocketId{};
        NodeGraphSocketDirection hitDirection = NodeGraphSocketDirection::Input;
        if (!nextHoveredSocketId.isValid() && extractSocketFromItem(hitItem, hitNodeId, hitSocketId, hitDirection)) {
            nextHoveredSocketId = hitSocketId;
        }

        QGraphicsItem* current = hitItem;
        while (current) {
            if (!nextHoveredEdgeId.isValid() && current->data(EdgeIdRole).isValid()) {
                nextHoveredEdgeId = itemEdgeId(current);
            }

            if (!nextHoveredNodeId.isValid() && current->data(NodeIdRole).isValid() &&
                dynamic_cast<NodeGraphSocketItem*>(current) == nullptr) {
                nextHoveredNodeId = itemNodeId(current);
            }

            current = current->parentItem();
        }

        if (nextHoveredEdgeId.isValid() && nextHoveredNodeId.isValid() && nextHoveredSocketId.isValid()) {
            break;
        }
    }

    if (hoveredEdgeId != nextHoveredEdgeId) {
        auto previousIt = edgeItemsById.find(hoveredEdgeId.value);
        if (hoveredEdgeId.isValid() && previousIt != edgeItemsById.end()) {
            setEdgeHovered(previousIt->second, false);
        }

        hoveredEdgeId = nextHoveredEdgeId;

        auto nextIt = edgeItemsById.find(hoveredEdgeId.value);
        if (hoveredEdgeId.isValid() && nextIt != edgeItemsById.end()) {
            setEdgeHovered(nextIt->second, true);
        }
    }

    if (hoveredNodeId != nextHoveredNodeId) {
        auto previousIt = nodeItemsById.find(hoveredNodeId.value);
        if (hoveredNodeId.isValid() && previousIt != nodeItemsById.end()) {
            setNodeHovered(previousIt->second, false);
        }

        hoveredNodeId = nextHoveredNodeId;

        auto nextIt = nodeItemsById.find(hoveredNodeId.value);
        if (hoveredNodeId.isValid() && nextIt != nodeItemsById.end()) {
            setNodeHovered(nextIt->second, true);
        }
    }

    if (hoveredSocketId != nextHoveredSocketId) {
        auto findSocketItem = [this](NodeGraphSocketId socketId) -> NodeGraphSocketItem* {
            auto inputIt = inputSocketItemsBySocket.find(socketId.value);
            if (socketId.isValid() && inputIt != inputSocketItemsBySocket.end()) {
                return inputIt->second;
            }

            auto outputIt = outputSocketItemsBySocket.find(socketId.value);
            if (socketId.isValid() && outputIt != outputSocketItemsBySocket.end()) {
                return outputIt->second;
            }

            return nullptr;
        };

        if (hoveredSocketId.isValid()) {
            setSocketHovered(findSocketItem(hoveredSocketId), false);
        }

        hoveredSocketId = nextHoveredSocketId;

        if (hoveredSocketId.isValid()) {
            setSocketHovered(findSocketItem(hoveredSocketId), true);
        }
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

void NodeGraphScene::setNodeHovered(NodeGraphNodeItem* item, bool hovered) {
    if (!item) {
        return;
    }

    item->setHoveredState(hovered);
    item->setZValue(hovered ? 1.1 : 1.0);
}

void NodeGraphScene::setEdgeHovered(QGraphicsPathItem* item, bool hovered) {
    if (!item) {
        return;
    }

    bool ok = false;
    const uint32_t rgba = static_cast<uint32_t>(item->data(EdgeBaseColorRole).toULongLong(&ok));
    const QColor baseColor = ok ? QColor::fromRgba(rgba) : nodegraphscene::edgeDefaultColor();
    QPen pen(hovered ? baseColor.lighter(nodegraphscene::edgeHoverLightenFactor) : baseColor, hovered ? nodegraphscene::edgeHoveredWidth : nodegraphscene::edgeDefaultWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    item->setPen(pen);
}

void NodeGraphScene::setSocketHovered(NodeGraphSocketItem* item, bool hovered) {
    if (!item) {
        return;
    }

    const QColor baseColor = nodegraphscene::valueTypeColor(item->valueType());
    const QColor fillColor = hovered ? baseColor.lighter(nodegraphscene::edgeHoverLightenFactor) : baseColor;
    const QColor outlineColor = hovered
        ? nodegraphscene::socketBorderColor().lighter(nodegraphscene::edgeHoverLightenFactor)
        : nodegraphscene::socketBorderColor();

    item->setBrush(fillColor);
    item->setPen(QPen(outlineColor, hovered ? nodegraphscene::socketBorderWidth + 0.75 : nodegraphscene::socketBorderWidth));
    item->setZValue(hovered ? 0.25 : 0.0);
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
        if (const NodeGraphSocketItem* socketItem = dynamic_cast<const NodeGraphSocketItem*>(current)) {
            outNodeId = socketItem->nodeId();
            outSocketId = socketItem->socketId();
            outDirection = socketItem->direction();
            return true;
        }

        current = current->parentItem();
    }

    return false;
}

bool NodeGraphScene::socketAtScenePos(const QPointF& scenePos, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId, NodeGraphSocketDirection& outDirection) const {
    constexpr qreal socketHitPadding = nodegraphscene::socketRadius + nodegraphscene::socketHoverPadding;
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

bool NodeGraphScene::handleNodeCapClick(const QPointF& scenePos) {
    QGraphicsItem* hitItem = itemAt(scenePos, QTransform());
    NodeGraphNodeItem* nodeItem = nullptr;
    QGraphicsItem* current = hitItem;
    while (current && !nodeItem) {
        nodeItem = dynamic_cast<NodeGraphNodeItem*>(current);
        current = current->parentItem();
    }

    if (!bridge || !nodeItem) {
        return false;
    }

    const nodegraphscene::NodeHitRegion region = nodeItem->hitRegionAt(nodeItem->mapFromScene(scenePos));
    const NodeGraphNodeId nodeId = nodeItem->nodeId();
    if (!nodeId.isValid()) {
        return false;
    }

    if (region == nodegraphscene::NodeHitRegion::LeftCap) {
        setSelectedNode(nodeId);
        if (editor.setNodeFrozen(nodeId, !nodeItem->frozen())) {
            reportStatus(nodeItem->frozen() ? "Node unfrozen." : "Node frozen.");
            applyPendingChanges();
        }
        return true;
    }

    if (region == nodegraphscene::NodeHitRegion::RightCap) {
        setSelectedNode(nodeId);
        if (editor.setNodeDisplayEnabled(nodeId, !nodeItem->displayEnabled())) {
            reportStatus(nodeItem->displayEnabled() ? "Node display disabled." : "Node display enabled.");
            applyPendingChanges();
        }
        return true;
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
