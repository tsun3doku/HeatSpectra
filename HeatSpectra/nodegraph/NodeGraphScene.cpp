#include "NodeGraphScene.hpp"
#include "NodeGraphNodeRectItem.hpp"
#include "render/SceneColorSpace.hpp"

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
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr qreal nodeWidth = 180.0;
constexpr qreal nodeHeight = 94.0;
constexpr qreal nodeHeaderHeight = nodeHeight * 0.25;
constexpr qreal socketStartY = nodeHeaderHeight + 16.0;
constexpr qreal socketSpacing = 18.0;
constexpr qreal socketRadius = 5.0;
const std::array<float, 4> clearColor = clearColorSRGBA();

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
    case NodeGraphValueType::Mesh:
        return QColor(113, 104, 232);
    case NodeGraphValueType::HeatReceiver:
        return QColor(250, 120, 96);
    case NodeGraphValueType::HeatSource:
        return QColor(247, 174, 92);
    case NodeGraphValueType::Point:
        return QColor(96, 204, 120);
    case NodeGraphValueType::Vector3:
        return QColor(92, 188, 224);
    case NodeGraphValueType::ScalarFloat:
        return QColor(241, 165, 76);
    case NodeGraphValueType::ScalarInt:
        return QColor(230, 142, 74);
    case NodeGraphValueType::ScalarBool:
        return QColor(235, 214, 97);
    case NodeGraphValueType::Unknown:
        return QColor(152, 162, 180);
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
    NodeGraphSocketDirection srcDirection = NodeGraphSocketDirection::Output,
    NodeGraphSocketDirection dstDirection = NodeGraphSocketDirection::Input) {
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

NodeGraphEdgeId findIncomingEdgeForInput(const NodeGraphState& state, NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    for (const NodeGraphEdge& edge : state.edges) {
        if (edge.toNode == nodeId && edge.toSocket == socketId) {
            return edge.id;
        }
    }

    return {};
}

const NodeGraphNode* findStateNodeById(const NodeGraphState& state, NodeGraphNodeId nodeId) {
    for (const NodeGraphNode& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

int findSocketIndexById(const std::vector<NodeGraphSocket>& sockets, NodeGraphSocketId socketId) {
    for (std::size_t index = 0; index < sockets.size(); ++index) {
        if (sockets[index].id == socketId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

NodeGraphSocketId socketByIndex(const std::vector<NodeGraphSocket>& sockets, int index) {
    if (index < 0 || index >= static_cast<int>(sockets.size())) {
        return {};
    }
    return sockets[static_cast<std::size_t>(index)].id;
}

std::vector<NodeGraphSocketId> matchingInputSocketsByType(const NodeGraphNode& node, NodeGraphValueType valueType) {
    std::vector<NodeGraphSocketId> sockets;
    for (const NodeGraphSocket& socket : node.inputs) {
        if (socket.valueType == valueType) {
            sockets.push_back(socket.id);
        }
    }
    return sockets;
}

int valueTypeOrdinalAtInputIndex(const NodeGraphNode& node, int inputIndex) {
    if (inputIndex < 0 || inputIndex >= static_cast<int>(node.inputs.size())) {
        return -1;
    }

    const NodeGraphValueType valueType = node.inputs[static_cast<std::size_t>(inputIndex)].valueType;
    int ordinal = 0;
    for (int index = 0; index <= inputIndex; ++index) {
        if (node.inputs[static_cast<std::size_t>(index)].valueType == valueType) {
            ++ordinal;
        }
    }
    return ordinal - 1;
}

}

NodeGraphScene::NodeGraphScene(QObject* parent)
    : QGraphicsScene(parent) {
    setSceneRect(-2000.0, -2000.0, 4000.0, 4000.0);
}

void NodeGraphScene::setBridge(NodeGraphBridge* bridgePtr) {
    bridge = bridgePtr;
    refreshFromGraph();
}

void NodeGraphScene::setNodeActivatedCallback(NodeActivatedCallback callback) {
    nodeActivatedCallback = std::move(callback);
}

void NodeGraphScene::setStatusCallback(StatusCallback callback) {
    statusCallback = std::move(callback);
}

void NodeGraphScene::refreshFromGraph() {
    clearActiveDragLine();
    hoveredNodeItem = nullptr;
    hoveredEdgeItem = nullptr;
    inputSocketItemsBySocket.clear();
    outputSocketItemsBySocket.clear();
    clear();

    if (!bridge) {
        return;
    }

    const NodeGraphState state = bridge->state();
    std::unordered_map<uint32_t, QRectF> nodeBoundsById;
    std::unordered_map<uint32_t, QPointF> inputAnchorBySocketId;
    std::unordered_map<uint32_t, QPointF> outputAnchorBySocketId;
    std::unordered_map<uint32_t, NodeGraphValueType> outputValueTypeBySocketId;

    for (const NodeGraphNode& node : state.nodes) {
        const QColor headerColor = categoryColor(node.category);
        const QColor nodeBorderColor = headerColor.lighter(150);
        QGraphicsRectItem* nodeRect = new NodeGraphNodeRectItem(
            QRectF(0.0, 0.0, nodeWidth, nodeHeight),
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

        QGraphicsRectItem* headerRect = new QGraphicsRectItem(0.0, 0.0, nodeWidth, nodeHeaderHeight, nodeRect);
        headerRect->setPen(QPen(Qt::NoPen));
        headerRect->setBrush(headerColor);
        setDecorativeItemFlags(headerRect);

        QGraphicsSimpleTextItem* titleItem = new QGraphicsSimpleTextItem(QString::fromStdString(node.title), nodeRect);
        titleItem->setBrush(QColor(244, 244, 244));
        titleItem->setPos(10.0, 8.0);
        setDecorativeItemFlags(titleItem);

        for (std::size_t index = 0; index < node.inputs.size(); ++index) {
            const NodeGraphSocket& socket = node.inputs[index];
            const qreal y = socketY(index);

            QGraphicsSimpleTextItem* inputItem = new QGraphicsSimpleTextItem(QString::fromStdString(socket.name), nodeRect);
            inputItem->setBrush(QColor(180, 190, 210));
            inputItem->setPos(12.0, y - 8.0);
            setDecorativeItemFlags(inputItem);

            QGraphicsEllipseItem* inputSocket = new QGraphicsEllipseItem(-socketRadius, y - socketRadius, socketRadius * 2.0, socketRadius * 2.0, nodeRect);
            inputSocket->setPen(QPen(QColor(24, 28, 38), 1.0));
            inputSocket->setBrush(valueTypeColor(socket.valueType));
            inputSocket->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
            inputSocket->setData(SocketRole, true);
            inputSocket->setData(SocketIdRole, static_cast<qulonglong>(socket.id.value));
            inputSocket->setData(SocketDirectionRole, static_cast<int>(NodeGraphSocketDirection::Input));
            setDecorativeItemFlags(inputSocket);

            inputAnchorBySocketId[socket.id.value] = nodeRect->mapToScene(QPointF(0.0, y));
            inputSocketItemsBySocket[socket.id.value] = inputSocket;
        }

        for (std::size_t index = 0; index < node.outputs.size(); ++index) {
            const NodeGraphSocket& socket = node.outputs[index];
            const qreal y = socketY(index);

            QGraphicsSimpleTextItem* outputItem = new QGraphicsSimpleTextItem(QString::fromStdString(socket.name), nodeRect);
            outputItem->setBrush(QColor(180, 210, 190));
            outputItem->setPos(nodeWidth - outputItem->boundingRect().width() - 12.0, y - 8.0);
            setDecorativeItemFlags(outputItem);

            QGraphicsEllipseItem* outputSocket = new QGraphicsEllipseItem(nodeWidth - socketRadius, y - socketRadius, socketRadius * 2.0, socketRadius * 2.0, nodeRect);
            outputSocket->setPen(QPen(QColor(24, 28, 38), 1.0));
            outputSocket->setBrush(valueTypeColor(socket.valueType));
            outputSocket->setData(NodeIdRole, static_cast<qulonglong>(node.id.value));
            outputSocket->setData(SocketRole, true);
            outputSocket->setData(SocketIdRole, static_cast<qulonglong>(socket.id.value));
            outputSocket->setData(SocketDirectionRole, static_cast<int>(NodeGraphSocketDirection::Output));
            setDecorativeItemFlags(outputSocket);

            outputAnchorBySocketId[socket.id.value] = nodeRect->mapToScene(QPointF(nodeWidth, y));
            outputValueTypeBySocketId[socket.id.value] = socket.valueType;
            outputSocketItemsBySocket[socket.id.value] = outputSocket;
        }

        nodeBoundsById[node.id.value] = nodeRect->sceneBoundingRect();
    }

    for (const NodeGraphEdge& edge : state.edges) {
        QPointF src{};
        QPointF dst{};

        const auto srcSocketIt = outputAnchorBySocketId.find(edge.fromSocket.value);
        const auto dstSocketIt = inputAnchorBySocketId.find(edge.toSocket.value);
        if (srcSocketIt != outputAnchorBySocketId.end() && dstSocketIt != inputAnchorBySocketId.end()) {
            src = srcSocketIt->second;
            dst = dstSocketIt->second;
        } else {
            const auto srcNodeIt = nodeBoundsById.find(edge.fromNode.value);
            const auto dstNodeIt = nodeBoundsById.find(edge.toNode.value);
            if (srcNodeIt == nodeBoundsById.end() || dstNodeIt == nodeBoundsById.end()) {
                continue;
            }

            src = outputAnchor(srcNodeIt->second);
            dst = inputAnchor(dstNodeIt->second);
        }

        QColor edgeColor(120, 200, 255);
        const auto valueTypeIt = outputValueTypeBySocketId.find(edge.fromSocket.value);
        if (valueTypeIt != outputValueTypeBySocketId.end()) {
            edgeColor = valueTypeColor(valueTypeIt->second).lighter(135);
        }

        QPen edgePen(edgeColor, 2.2);
        edgePen.setCapStyle(Qt::RoundCap);
        edgePen.setJoinStyle(Qt::RoundJoin);
        QGraphicsPathItem* line = addPath(buildEdgePath(src, dst), edgePen);
        line->setZValue(-1.0);
        line->setData(EdgeIdRole, static_cast<qulonglong>(edge.id.value));
        line->setData(EdgeFromSocketRole, static_cast<qulonglong>(edge.fromSocket.value));
        line->setData(EdgeToSocketRole, static_cast<qulonglong>(edge.toSocket.value));
        line->setData(EdgeBaseColorRole, static_cast<qulonglong>(edgeColor.rgba()));
    }
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

        CopiedNode copiedNode{};
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
        [](const CopiedNode& lhs, const CopiedNode& rhs) {
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

    std::unordered_map<uint32_t, NodeGraphNodeId> newNodeIdBySourceNodeId;
    std::unordered_map<uint32_t, NodeGraphSocketId> newOutputSocketBySourceOutputSocket;
    std::vector<NodeGraphNodeId> createdNodeIds;
    createdNodeIds.reserve(copiedNodes.size());

    for (const CopiedNode& copiedNode : copiedNodes) {
        const NodeGraphNodeId newNodeId = bridge->addNode(
            copiedNode.typeId,
            copiedNode.title,
            copiedNode.x + positionOffset,
            copiedNode.y + positionOffset);
        if (!newNodeId.isValid()) {
            continue;
        }

        newNodeIdBySourceNodeId[copiedNode.sourceNodeId.value] = newNodeId;
        createdNodeIds.push_back(newNodeId);

        NodeGraphNode newNode{};
        if (!bridge->getNode(newNodeId, newNode)) {
            continue;
        }

        const std::size_t outputSocketCount = std::min(copiedNode.outputSocketIds.size(), newNode.outputs.size());
        for (std::size_t index = 0; index < outputSocketCount; ++index) {
            newOutputSocketBySourceOutputSocket[copiedNode.outputSocketIds[index].value] = newNode.outputs[index].id;
        }

        const NodeTypeDefinition* nodeDefinition = findNodeTypeDefinitionById(canonicalNodeTypeId(copiedNode.typeId));
        for (const NodeGraphParamValue& originalParameter : copiedNode.parameters) {
            NodeGraphParamValue parameter = originalParameter;

            if (nodeDefinition) {
                const NodeGraphParamDefinition* parameterDefinition = findNodeParamDefinition(*nodeDefinition, parameter.id);
                if (parameterDefinition && parameterDefinition->isAction) {
                    parameter = makeNodeGraphParamValue(*parameterDefinition);
                }
            }

            bridge->setNodeParameter(newNodeId, parameter);
        }
    }

    if (createdNodeIds.empty()) {
        return false;
    }

    std::vector<CopiedEdge> sortedEdges = copiedEdges;
    const NodeGraphState originalState = bridge->state();
    std::sort(
        sortedEdges.begin(),
        sortedEdges.end(),
        [&originalState](const CopiedEdge& lhs, const CopiedEdge& rhs) {
            const NodeGraphNode* lhsToNode = findStateNodeById(originalState, lhs.toNode);
            const NodeGraphNode* rhsToNode = findStateNodeById(originalState, rhs.toNode);
            const int lhsSocketIndex = lhsToNode ? findSocketIndexById(lhsToNode->inputs, lhs.toSocket) : -1;
            const int rhsSocketIndex = rhsToNode ? findSocketIndexById(rhsToNode->inputs, rhs.toSocket) : -1;
            if (lhs.toNode.value != rhs.toNode.value) {
                return lhs.toNode.value < rhs.toNode.value;
            }
            return lhsSocketIndex < rhsSocketIndex;
        });

    for (const CopiedEdge& copiedEdge : sortedEdges) {
        const auto fromNodeIt = newNodeIdBySourceNodeId.find(copiedEdge.fromNode.value);
        const auto toNodeIt = newNodeIdBySourceNodeId.find(copiedEdge.toNode.value);
        if (fromNodeIt == newNodeIdBySourceNodeId.end() || toNodeIt == newNodeIdBySourceNodeId.end()) {
            continue;
        }

        const auto fromSocketIt = newOutputSocketBySourceOutputSocket.find(copiedEdge.fromSocket.value);
        if (fromSocketIt == newOutputSocketBySourceOutputSocket.end()) {
            continue;
        }

        NodeGraphSocketId targetInputSocket{};
        const NodeGraphNode* oldTargetNode = findStateNodeById(originalState, copiedEdge.toNode);
        if (!oldTargetNode) {
            continue;
        }

        const int oldInputSocketIndex = findSocketIndexById(oldTargetNode->inputs, copiedEdge.toSocket);
        if (oldInputSocketIndex < 0) {
            continue;
        }

        NodeGraphNode newTargetNode{};
        if (!bridge->getNode(toNodeIt->second, newTargetNode)) {
            continue;
        }

        targetInputSocket = socketByIndex(newTargetNode.inputs, oldInputSocketIndex);
        if (!targetInputSocket.isValid()) {
            const NodeGraphSocket& oldInputSocket = oldTargetNode->inputs[static_cast<std::size_t>(oldInputSocketIndex)];
            const int oldOrdinal = valueTypeOrdinalAtInputIndex(*oldTargetNode, oldInputSocketIndex);
            if (oldOrdinal >= 0) {
                const std::vector<NodeGraphSocketId> matchingSockets =
                    matchingInputSocketsByType(newTargetNode, oldInputSocket.valueType);
                if (oldOrdinal < static_cast<int>(matchingSockets.size())) {
                    targetInputSocket = matchingSockets[static_cast<std::size_t>(oldOrdinal)];
                }
            }
        }

        if (!targetInputSocket.isValid()) {
            continue;
        }

        std::string errorMessage;
        bridge->connectSockets(
            fromNodeIt->second,
            fromSocketIt->second,
            toNodeIt->second,
            targetInputSocket,
            errorMessage);
    }

    refreshFromGraph();
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
        removedAny = bridge->removeNode(nodeId) || removedAny;
    }

    if (removedAny) {
        refreshFromGraph();
    }

    return removedAny;
}

void NodeGraphScene::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsScene::drawBackground(painter, rect);

    painter->save();
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
            if (bridge->removeConnection(edgeId)) {
                reportStatus("Connection removed.");
                refreshFromGraph();
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
            const NodeGraphEdgeId existingIncomingEdge = findIncomingEdgeForInput(bridge->state(), nodeId, socketId);
            if (existingIncomingEdge.isValid()) {
                if (bridge->removeConnection(existingIncomingEdge)) {
                    reportStatus("Input disconnected. Drop on an output socket to reconnect.");
                    refreshFromGraph();
                }
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
        activeDragLine = addPath(buildEdgePath(activeDragStartPos, event->scenePos(), activeDragFromDirection, previewEndDirection), previewPen);
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
        activeDragLine->setPath(buildEdgePath(activeDragStartPos, event->scenePos(), activeDragFromDirection, previewEndDirection));
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
                if (bridge->connectSockets(fromNode, fromSocket, targetNode, targetSocket, errorMessage)) {
                    reportStatus("Sockets connected.");
                    refreshFromGraph();
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
        changed = bridge->moveNode(nodeId, static_cast<float>(position.x()), static_cast<float>(position.y())) || changed;
    }

    if (changed) {
        refreshFromGraph();
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
        edgeItem->setPath(buildEdgePath(src, dst));
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

bool NodeGraphScene::extractSocketFromItem(
    const QGraphicsItem* item,
    NodeGraphNodeId& outNodeId,
    NodeGraphSocketId& outSocketId,
    NodeGraphSocketDirection& outDirection) {
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

bool NodeGraphScene::socketAtScenePos(
    const QPointF& scenePos,
    NodeGraphNodeId& outNodeId,
    NodeGraphSocketId& outSocketId,
    NodeGraphSocketDirection& outDirection) const {
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
