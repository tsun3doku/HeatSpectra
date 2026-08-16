#include "NodeGraphCanvasItem.hpp"

#include "NodeGraphUiModel.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/ui/scene/NodeGraphSceneStyle.hpp"
#include "ui/UiTypography.hpp"

#include <QtCore/QAbstractItemModel>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QVariantMap>
#include <QtGui/QCursor>
#include <QtGui/QFontMetricsF>
#include <QtGui/QHoverEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QRadialGradient>
#include <QtGui/QWheelEvent>

#include <algorithm>
#include <cmath>

static QColor graphBlendColor(const QColor& first, const QColor& second, qreal amount) {
    const qreal factor = std::clamp(amount, 0.0, 1.0);
    return QColor(
        static_cast<int>(first.red() + (second.red() - first.red()) * factor),
        static_cast<int>(first.green() + (second.green() - first.green()) * factor),
        static_cast<int>(first.blue() + (second.blue() - first.blue()) * factor),
        static_cast<int>(first.alpha() + (second.alpha() - first.alpha()) * factor));
}

static QString graphIconFolder(const QString& typeId) {
    if (typeId == QStringLiteral("contact")) return QStringLiteral("Contact");
    if (typeId == QStringLiteral("heat_solve")) return QStringLiteral("HeatSystem");
    if (typeId == QStringLiteral("model")) return QStringLiteral("Model");
    if (typeId == QStringLiteral("heat_model")) return QStringLiteral("HeatModel");
    if (typeId == QStringLiteral("remesh")) return QStringLiteral("Remesh");
    if (typeId == QStringLiteral("transform")) return QStringLiteral("Transform");
    if (typeId == QStringLiteral("voronoi")) return QStringLiteral("VoronoiSystem");
    if (typeId == QStringLiteral("mesh_points") || typeId == QStringLiteral("points")) return QStringLiteral("Points");
    return {};
}

static QImage graphIconImage(const QString& typeId) {
    static QHash<QString, QImage> imageCache;
    if (typeId.isEmpty()) return {};
    const auto existing = imageCache.find(typeId);
    if (existing != imageCache.end()) return existing.value();
    const QString folder = graphIconFolder(typeId);
    if (folder.isEmpty()) {
        imageCache.insert(typeId, {});
        return {};
    }
    const QString relative = QStringLiteral("textures/icons/%1/128w/Artboard 1.png").arg(folder);
    const QStringList candidates = {
        QDir::current().filePath(relative),
        QDir::current().filePath(QStringLiteral("ParaMetal/") + relative),
        QDir::current().filePath(QStringLiteral("../") + relative),
        QDir::current().filePath(QStringLiteral("../../") + relative)
    };
    for (const QString& candidate : candidates) {
        QImage image(candidate);
        if (!image.isNull()) {
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            imageCache.insert(typeId, image);
            return image;
        }
    }
    imageCache.insert(typeId, {});
    return {};
}

static void paintArcText(
    QPainter& painter,
    const QString& text,
    const QPointF& center,
    qreal radius,
    qreal centerAngleDegrees,
    qreal maxSweepDegrees,
    qreal baselineInset,
    bool reverseDirection) {
    constexpr qreal pi = 3.14159265358979323846;
    QFont arcFont = ui::UiTypography::font(ui::TextRole::Title);
    arcFont.setPixelSize(arcFont.pixelSize() + 3);
    arcFont.setWeight(QFont::Bold);
    arcFont.setLetterSpacing(QFont::AbsoluteSpacing, arcFont.letterSpacing() + 2.5);
    painter.setFont(arcFont);

    const QFontMetricsF metrics(arcFont);
    qreal totalAdvance = 0.0;
    for (const QChar character : text) {
        totalAdvance += metrics.horizontalAdvance(QString(character));
    }

    const qreal direction = reverseDirection ? -1.0 : 1.0;
    const qreal maxSweepRadians = maxSweepDegrees * pi / 180.0;
    const qreal naturalSweepRadians = totalAdvance / radius;
    const qreal usedSweepRadians = std::min(maxSweepRadians, naturalSweepRadians);
    const qreal sweepScale = naturalSweepRadians > 0.0
        ? usedSweepRadians / naturalSweepRadians
        : 1.0;
    const qreal centerAngleRadians = centerAngleDegrees * pi / 180.0;
    const qreal firstAngleRadians = centerAngleRadians - direction * usedSweepRadians * 0.5;
    const qreal baselineOffset = direction > 0.0
        ? baselineInset
        : metrics.ascent() - metrics.descent() - baselineInset;

    qreal advanceBefore = 0.0;
    for (const QChar character : text) {
        const QString glyph(character);
        const qreal advance = metrics.horizontalAdvance(glyph);
        const qreal angle = firstAngleRadians + direction *
            (advanceBefore + advance * 0.5) / radius * sweepScale;
        const QPointF position(
            center.x() + std::cos(angle) * radius,
            center.y() + std::sin(angle) * radius);

        painter.save();
        painter.translate(position);
        painter.rotate(angle * 180.0 / pi + (direction > 0.0 ? 90.0 : -90.0));
        painter.drawText(QPointF(-advance * 0.5, baselineOffset), glyph);
        painter.restore();

        advanceBefore += advance;
    }
}

static void paintGraphCap(
    QPainter& painter,
    const QPainterPath& path,
    const QColor& activeColor,
    const QColor& inactiveColor,
    bool active,
    bool hovered) {
    const QRectF rect = path.boundingRect();
    const QColor fill = hovered ? graphBlendColor(inactiveColor, activeColor, 0.5) :
                                  (active ? activeColor : inactiveColor);
    if (active) {
        QRadialGradient glow(rect.center(), std::max(rect.width(), rect.height()) * nodegraphscene::capGlowRadiusMultiplier);
        QColor start = activeColor;
        start.setAlpha(nodegraphscene::capGlowAlpha);
        QColor end = activeColor;
        end.setAlpha(0);
        glow.setColorAt(0.0, start);
        glow.setColorAt(1.0, end);
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(rect.center(), nodegraphscene::capGlowRadius, nodegraphscene::capGlowRadius);
    }
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, fill.lighter(active ? nodegraphscene::capActiveLighten : 100));
    gradient.setColorAt(1.0, fill.darker(active ? nodegraphscene::capActiveDarken : nodegraphscene::capInactiveDarken));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPath(path);
}

NodeGraphCanvasItem::NodeGraphCanvasItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setFocus(true);
    setAntialiasing(true);
    setMipmap(false);
    setOpaquePainting(false);
    setRenderTarget(QQuickPaintedItem::Image);
    setFillColor(Qt::transparent);
}

NodeGraphUiModel* NodeGraphCanvasItem::model() const { return graphModel; }

void NodeGraphCanvasItem::setModel(NodeGraphUiModel* updatedModel) {
    if (graphModel == updatedModel) return;
    if (graphModel) disconnect(graphModel, nullptr, this, nullptr);
    graphModel = updatedModel;
    if (graphModel) {
        connect(graphModel, &QAbstractItemModel::modelReset, this, &NodeGraphCanvasItem::refreshModel);
        connect(graphModel, &QAbstractItemModel::dataChanged, this, &NodeGraphCanvasItem::refreshModel);
        connect(graphModel, &NodeGraphUiModel::edgesChanged, this, &NodeGraphCanvasItem::refreshModel);
    }
    refreshModel();
    emit modelChanged();
}

void NodeGraphCanvasItem::refreshModel() {
    nodes.clear();
    edges.clear();
    if (graphModel) {
        nodes.reserve(graphModel->rowCount());
        for (int row = 0; row < graphModel->rowCount(); ++row) {
            const QModelIndex index = graphModel->index(row, 0);
            Node node;
            node.id = graphModel->data(index, NodeGraphUiModel::NodeIdRole).toInt();
            node.title = graphModel->data(index, NodeGraphUiModel::TitleRole).toString();
            node.typeId = graphModel->data(index, NodeGraphUiModel::TypeRole).toString();
            node.icon = graphIconImage(node.typeId);
            node.x = graphModel->data(index, NodeGraphUiModel::NodeXRole).toReal();
            node.y = graphModel->data(index, NodeGraphUiModel::NodeYRole).toReal();
            node.displayEnabled = graphModel->data(index, NodeGraphUiModel::DisplayEnabledRole).toBool();
            node.frozen = graphModel->data(index, NodeGraphUiModel::FrozenRole).toBool();
            node.selected = graphModel->data(index, NodeGraphUiModel::SelectedRole).toBool();
            const auto readSockets = [](const QVariantList& source, std::vector<Socket>& destination) {
                destination.reserve(source.size());
                for (const QVariant& value : source) {
                    const QVariantMap map = value.toMap();
                    Socket socket;
                    socket.id = map.value(QStringLiteral("socketId")).toInt();
                    socket.valueType = map.value(QStringLiteral("valueType")).toInt();
                    const QVariantList accepted = map.value(QStringLiteral("acceptedValueTypes")).toList();
                    socket.acceptedValueTypes.reserve(accepted.size());
                    for (const QVariant& acceptedType : accepted) {
                        socket.acceptedValueTypes.push_back(acceptedType.toInt());
                    }
                    destination.push_back(socket);
                }
            };
            readSockets(graphModel->data(index, NodeGraphUiModel::InputsRole).toList(), node.inputs);
            readSockets(graphModel->data(index, NodeGraphUiModel::OutputsRole).toList(), node.outputs);
            nodes.push_back(node);
        }
        for (const QVariant& value : graphModel->edges()) {
            const QVariantMap map = value.toMap();
            edges.push_back({map.value(QStringLiteral("edgeId")).toInt(),
                             map.value(QStringLiteral("fromNode")).toInt(),
                             map.value(QStringLiteral("fromSocket")).toInt(),
                             map.value(QStringLiteral("toNode")).toInt(),
                             map.value(QStringLiteral("toSocket")).toInt(),
                             map.value(QStringLiteral("valueType")).toInt()});
        }
    }
    requestRender();
}

void NodeGraphCanvasItem::requestRender() { update(); }

bool NodeGraphCanvasItem::isSimulationNode(const Node& node) {
    return node.typeId == QString::fromLatin1(nodegraphtypes::HeatSolve);
}

void NodeGraphCanvasItem::paintSimulationHighlight(QPainter& painter, const Node& node) const {
    const QPointF center(
        node.x + nodegraphscene::nodeWidth * 0.5,
        node.y + nodegraphscene::nodeHeight * 0.5);
    const qreal halfDiagonal = std::hypot(
        nodegraphscene::nodeWidth * 0.5,
        nodegraphscene::nodeHeight * 0.5);
    const qreal ringRadius = halfDiagonal + nodegraphscene::simulationRingPadding;

    QColor ringColor = nodegraphscene::simulationRingColor();
    if (node.selected || node.id == hoveredNodeId) ringColor = ringColor.lighter(120);

    painter.save();
    painter.setPen(QPen(ringColor, nodegraphscene::simulationRingWidth,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, ringRadius, ringRadius);

    // Remove both labels from the solid circle so the graph background shows through them.
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.setPen(QColor(0, 0, 0, 255));
    paintArcText(
        painter,
        QStringLiteral("SIMULATION"),
        center,
        ringRadius - nodegraphscene::simulationLabelRadiusInset,
        nodegraphscene::simulationLabelCenterAngle,
        nodegraphscene::simulationLabelMaxSweep,
        nodegraphscene::simulationLabelBaselineInset,
        false);
    paintArcText(
        painter,
        QStringLiteral("NODE"),
        center,
        ringRadius - nodegraphscene::simulationLabelRadiusInset,
        nodegraphscene::simulationNodeLabelCenterAngle,
        nodegraphscene::simulationNodeLabelMaxSweep,
        nodegraphscene::simulationLabelBaselineInset,
        true);
    painter.restore();
}

void NodeGraphCanvasItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (!panInitialized && newGeometry.width() > 0.0) {
        pan = QPointF(newGeometry.width() * 0.5, 55.0);
        panInitialized = true;
    }
    requestRender();
}

QPointF NodeGraphCanvasItem::graphPosition(const QPointF& position) const { return (position - pan) / zoomValue; }
QPointF NodeGraphCanvasItem::itemPosition(const QPointF& position) const { return pan + position * zoomValue; }

QPointF NodeGraphCanvasItem::socketPosition(const Node& node, bool output, int index) const {
    const std::size_t count = output ? node.outputs.size() : node.inputs.size();
    const QPointF local = output
        ? nodegraphscene::outputSocketPosition(static_cast<std::size_t>(index), count)
        : nodegraphscene::inputSocketPosition(static_cast<std::size_t>(index), count);
    return QPointF(node.x, node.y) + local;
}

QPointF NodeGraphCanvasItem::socketPositionById(const Node& node, bool output, int socketId) const {
    const auto& sockets = output ? node.outputs : node.inputs;
    for (int index = 0; index < static_cast<int>(sockets.size()); ++index) {
        if (sockets[index].id == socketId) return socketPosition(node, output, index);
    }
    const QRectF nodeRect(node.x, node.y, nodegraphscene::nodeWidth, nodegraphscene::nodeHeight);
    return output ? nodegraphscene::outputAnchor(nodeRect) : nodegraphscene::inputAnchor(nodeRect);
}

const NodeGraphCanvasItem::Node* NodeGraphCanvasItem::findNode(int nodeId) const {
    const auto it = std::find_if(nodes.begin(), nodes.end(), [nodeId](const Node& node) { return node.id == nodeId; });
    return it == nodes.end() ? nullptr : &*it;
}

NodeGraphCanvasItem::Node* NodeGraphCanvasItem::findNode(int nodeId) {
    const auto it = std::find_if(nodes.begin(), nodes.end(), [nodeId](const Node& node) { return node.id == nodeId; });
    return it == nodes.end() ? nullptr : &*it;
}

int NodeGraphCanvasItem::nodeIndexAt(const QPointF& position) const {
    for (int index = static_cast<int>(nodes.size()) - 1; index >= 0; --index) {
        if (QRectF(nodes[index].x, nodes[index].y, nodegraphscene::nodeWidth, nodegraphscene::nodeHeight).contains(position)) return index;
    }
    return -1;
}

int NodeGraphCanvasItem::socketIndexAt(const Node& node, bool output, const QPointF& position) const {
    const auto& sockets = output ? node.outputs : node.inputs;
    const qreal hitRadius = nodegraphscene::socketRadius * zoomValue + nodegraphscene::socketHoverPadding;
    for (int index = 0; index < static_cast<int>(sockets.size()); ++index) {
        if (QLineF(itemPosition(socketPosition(node, output, index)), position).length() <= hitRadius) return index;
    }
    return -1;
}

int NodeGraphCanvasItem::edgeIndexAt(const QPointF& position) const {
    QPainterPathStroker stroker;
    stroker.setWidth(nodegraphscene::edgeHoveredWidth / std::max(zoomValue, 0.001));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    for (int index = static_cast<int>(edges.size()) - 1; index >= 0; --index) {
        const Edge& edge = edges[index];
        const Node* from = findNode(edge.fromNode);
        const Node* to = findNode(edge.toNode);
        if (!from || !to) continue;
        if (stroker.createStroke(nodegraphscene::buildEdgePath(
                socketPositionById(*from, true, edge.fromSocket),
                socketPositionById(*to, false, edge.toSocket))).contains(position)) {
            return index;
        }
    }
    return -1;
}

void NodeGraphCanvasItem::updateHover(const QPointF& position) {
    const int index = nodeIndexAt(position);
    const int nodeId = index >= 0 ? nodes[index].id : 0;
    int cap = 0;
    int socketNodeId = 0;
    int socketId = 0;
    bool socketOutput = false;
    bool socketCompatible = true;
    const QPointF item = itemPosition(position);
    const Node* source = interaction == Interaction::Connect ? findNode(interactionNodeId) : nullptr;
    const Socket* sourceSocket = nullptr;
    if (source) {
        const auto& sourceSockets = connectionFromOutput ? source->outputs : source->inputs;
        for (const Socket& socket : sourceSockets) {
            if (socket.id == interactionSocketId) { sourceSocket = &socket; break; }
        }
    }
    for (const Node& node : nodes) {
        const bool targetOutput = source ? !connectionFromOutput : false;
        int socketIndex = socketIndexAt(node, targetOutput, item);
        bool foundOutput = source ? targetOutput : false;
        if (!source && socketIndex < 0) {
            socketIndex = socketIndexAt(node, true, item);
            foundOutput = true;
        }
        if (socketIndex >= 0) {
            const Socket& socket = (foundOutput ? node.outputs : node.inputs)[socketIndex];
            socketNodeId = node.id;
            socketId = socket.id;
            socketOutput = foundOutput;
            if (source && sourceSocket) {
                socketCompatible = socketTypesCompatible(*sourceSocket, socket) &&
                    node.id != source->id;
                for (const Edge& edge : edges) {
                    const bool sameDirection = connectionFromOutput &&
                        edge.fromNode == source->id && edge.fromSocket == interactionSocketId &&
                        edge.toNode == node.id && edge.toSocket == socket.id;
                    const bool reverseDirection = !connectionFromOutput &&
                        edge.fromNode == node.id && edge.fromSocket == socket.id &&
                        edge.toNode == source->id && edge.toSocket == interactionSocketId;
                    if (sameDirection || reverseDirection) socketCompatible = false;
                }
            }
            break;
        }
    }
    if (index >= 0) {
        const QPointF local = position - QPointF(nodes[index].x, nodes[index].y);
        const nodegraphscene::NodeHitRegion region = nodegraphscene::hitRegionAt(local);
        if (region == nodegraphscene::NodeHitRegion::LeftCap) cap = 1;
        else if (region == nodegraphscene::NodeHitRegion::RightCap) cap = 2;
    }
    const int edgeIndex = socketNodeId == 0 && cap == 0 ? edgeIndexAt(position) : -1;
    if (nodeId != hoveredNodeId || cap != hoveredCap ||
        socketNodeId != hoveredSocketNodeId || socketId != hoveredSocketId ||
        socketOutput != hoveredSocketOutput || socketCompatible != hoveredSocketCompatible ||
        edgeIndex != hoveredEdgeIndex) {
        hoveredNodeId = nodeId;
        hoveredCap = cap;
        hoveredSocketNodeId = socketNodeId;
        hoveredSocketId = socketId;
        hoveredSocketOutput = socketOutput;
        hoveredSocketCompatible = socketCompatible;
        hoveredEdgeIndex = edgeIndex;
        setCursor(QCursor(cap != 0 || socketNodeId != 0 || edgeIndex >= 0
                              ? Qt::PointingHandCursor : Qt::ArrowCursor));
        requestRender();
    }
}

bool NodeGraphCanvasItem::socketTypesCompatible(const Socket& source, const Socket& target) {
    const auto contains = [](const std::vector<int>& values, int value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };

    if (source.valueType != 0 && target.valueType != 0) {
        return source.valueType == target.valueType;
    }
    if (!source.acceptedValueTypes.empty()) {
        if (target.valueType != 0) {
            return contains(source.acceptedValueTypes, target.valueType);
        }
        if (!target.acceptedValueTypes.empty()) {
            return std::any_of(source.acceptedValueTypes.begin(), source.acceptedValueTypes.end(),
                [&target, &contains](int value) { return contains(target.acceptedValueTypes, value); });
        }
    }
    if (!target.acceptedValueTypes.empty() && source.valueType != 0) {
        return contains(target.acceptedValueTypes, source.valueType);
    }
    return source.valueType == target.valueType;
}

void NodeGraphCanvasItem::hoverMoveEvent(QHoverEvent* event) {
    if (event) { updateHover(graphPosition(event->position())); event->accept(); }
}

void NodeGraphCanvasItem::hoverLeaveEvent(QHoverEvent* event) {
    hoveredNodeId = 0;
    hoveredCap = 0;
    hoveredSocketNodeId = 0;
    hoveredSocketId = 0;
    hoveredSocketOutput = false;
    hoveredSocketCompatible = true;
    hoveredEdgeIndex = -1;
    unsetCursor();
    requestRender();
    if (event) event->accept();
}

void NodeGraphCanvasItem::mousePressEvent(QMouseEvent* event) {
    if (!event) return;
    forceActiveFocus();
    pressItemPosition = lastItemPosition = event->position();
    if (event->button() == Qt::MiddleButton) {
        interaction = Interaction::Pan;
        event->accept();
        return;
    }
    const QPointF graph = graphPosition(event->position());
    int nodeIndex = nodeIndexAt(graph);
    if (nodeIndex < 0) {
        for (int index = static_cast<int>(nodes.size()) - 1; index >= 0; --index) {
            if (socketIndexAt(nodes[index], false, event->position()) >= 0 ||
                socketIndexAt(nodes[index], true, event->position()) >= 0) {
                nodeIndex = index;
                break;
            }
        }
    }
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ControlModifier)) {
        const int edgeIndex = edgeIndexAt(graph);
        if (edgeIndex >= 0 && graphModel) {
            graphModel->removeConnection(edges[edgeIndex].id);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::RightButton) {
        if (nodeIndex >= 0) emit nodeMenuRequested(nodes[nodeIndex].id);
        else emit createMenuRequested(graph.x(), graph.y());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !graphModel) return;
    if (nodeIndex < 0) {
        interaction = Interaction::BoxSelect;
        selectionStart = graph;
        selectionEnd = graph;
        selectionAdditive = event->modifiers().testFlag(Qt::ShiftModifier) ||
                            event->modifiers().testFlag(Qt::ControlModifier);
        if (!selectionAdditive) {
            graphModel->setSelectedNodeId(0);
        }
        requestRender();
        event->accept();
        return;
    }
    Node& node = nodes[nodeIndex];
    int socketIndex = socketIndexAt(node, false, event->position());
    bool output = false;
    if (socketIndex < 0) { socketIndex = socketIndexAt(node, true, event->position()); output = true; }
    if (socketIndex >= 0) {
        if (!output) {
            for (const Edge& edge : edges) {
                if (edge.toNode == node.id && edge.toSocket == node.inputs[socketIndex].id) {
                    graphModel->removeConnection(edge.id);
                    break;
                }
            }
        }
        interaction = Interaction::Connect;
        interactionNodeId = node.id;
        interactionSocketId = (output ? node.outputs : node.inputs)[socketIndex].id;
        connectionFromOutput = output;
        connectionEnd = event->position();
        updateHover(graphPosition(event->position()));
    } else {
        const nodegraphscene::NodeHitRegion region = nodegraphscene::hitRegionAt(graph - QPointF(node.x, node.y));
        if (region == nodegraphscene::NodeHitRegion::LeftCap) graphModel->toggleNodeFrozen(node.id);
        else if (region == nodegraphscene::NodeHitRegion::RightCap) graphModel->toggleNodeDisplay(node.id);
        else {
            const int nodeId = node.id;
            const bool additive = event->modifiers().testFlag(Qt::ShiftModifier) ||
                                  event->modifiers().testFlag(Qt::ControlModifier);
            const bool wasSelected = graphModel->isNodeSelected(nodeId);
            if (additive || !wasSelected) {
                graphModel->setNodeSelected(nodeId, additive);
            }
            dragSelectionOrigins.clear();
            if (additive && wasSelected) {
                interaction = Interaction::None;
            } else {
                for (const Node& selectedNode : nodes) {
                    if (graphModel->isNodeSelected(selectedNode.id)) {
                        dragSelectionOrigins.emplace_back(selectedNode.id,
                            QPointF(selectedNode.x, selectedNode.y));
                    }
                }
                interaction = Interaction::MoveNode;
            }
            interactionNodeId = nodeId;
        }
    }
    requestRender();
    event->accept();
}

void NodeGraphCanvasItem::mouseMoveEvent(QMouseEvent* event) {
    if (!event) return;
    const QPointF delta = event->position() - lastItemPosition;
    lastItemPosition = event->position();
    if (interaction == Interaction::Pan) {
        pan += delta;
    } else if (interaction == Interaction::MoveNode) {
        const QPointF total = (event->position() - pressItemPosition) / zoomValue;
        for (const auto& [nodeId, origin] : dragSelectionOrigins) {
            if (Node* node = findNode(nodeId)) {
                node->x = origin.x() + total.x();
                node->y = origin.y() + total.y();
            }
        }
    } else if (interaction == Interaction::Connect) {
        connectionEnd = event->position();
        updateHover(graphPosition(event->position()));
    } else if (interaction == Interaction::BoxSelect) {
        selectionEnd = graphPosition(event->position());
    } else {
        updateHover(graphPosition(event->position()));
    }
    requestRender();
    event->accept();
}

void NodeGraphCanvasItem::finishConnection(const QPointF& position) {
    const Node* source = findNode(interactionNodeId);
    if (!source) return;
    int targetNode = 0;
    int targetSocket = 0;
    qreal best = 11.0;
    const Socket* sourceSocket = nullptr;
    const auto& sourceSockets = connectionFromOutput ? source->outputs : source->inputs;
    for (const Socket& socket : sourceSockets) {
        if (socket.id == interactionSocketId) { sourceSocket = &socket; break; }
    }
    if (!sourceSocket) return;
    for (const Node& node : nodes) {
        const auto& sockets = connectionFromOutput ? node.inputs : node.outputs;
        for (int index = 0; index < static_cast<int>(sockets.size()); ++index) {
            if (!socketTypesCompatible(*sourceSocket, sockets[index])) continue;
            if (node.id == source->id) continue;
            bool alreadyConnected = false;
            for (const Edge& edge : edges) {
                const bool sameDirection = connectionFromOutput &&
                    edge.fromNode == source->id && edge.fromSocket == interactionSocketId &&
                    edge.toNode == node.id && edge.toSocket == sockets[index].id;
                const bool reverseDirection = !connectionFromOutput &&
                    edge.fromNode == node.id && edge.fromSocket == sockets[index].id &&
                    edge.toNode == source->id && edge.toSocket == interactionSocketId;
                alreadyConnected = sameDirection || reverseDirection;
                if (alreadyConnected) break;
            }
            if (alreadyConnected) continue;
            const qreal distance = QLineF(itemPosition(socketPosition(node, !connectionFromOutput, index)), position).length();
            if (distance < best) { best = distance; targetNode = node.id; targetSocket = sockets[index].id; }
        }
    }
    if (targetNode > 0 && graphModel) {
        if (connectionFromOutput) graphModel->connectSockets(source->id, interactionSocketId, targetNode, targetSocket);
        else graphModel->connectSockets(targetNode, targetSocket, source->id, interactionSocketId);
    }
}

void NodeGraphCanvasItem::mouseReleaseEvent(QMouseEvent* event) {
    if (!event) return;
    if (interaction == Interaction::MoveNode) {
        for (const auto& selection : dragSelectionOrigins) {
            if (const Node* node = findNode(selection.first)) graphModel->moveNode(node->id, node->x, node->y);
        }
    } else if (interaction == Interaction::Connect) {
        finishConnection(event->position());
    } else if (interaction == Interaction::BoxSelect && graphModel) {
        const QRectF selection = QRectF(selectionStart, selectionEnd).normalized();
        std::vector<int> selectedNodeIds;
        for (const Node& node : nodes) {
            const QRectF nodeRect(node.x, node.y, nodegraphscene::nodeWidth, nodegraphscene::nodeHeight);
            if (selection.intersects(nodeRect) &&
                (!selectionAdditive || !graphModel->isNodeSelected(node.id))) {
                selectedNodeIds.push_back(node.id);
            }
        }
        if (selectedNodeIds.empty()) {
            if (!selectionAdditive) graphModel->setSelectedNodeId(0);
        } else {
            bool additive = selectionAdditive;
            for (int nodeId : selectedNodeIds) {
                graphModel->setNodeSelected(nodeId, additive);
                additive = true;
            }
        }
    }
    interaction = Interaction::None;
    selectionAdditive = false;
    dragSelectionOrigins.clear();
    interactionNodeId = 0;
    interactionSocketId = 0;
    requestRender();
    event->accept();
}

void NodeGraphCanvasItem::wheelEvent(QWheelEvent* event) {
    if (!event || event->angleDelta().y() == 0) return;
    const QPointF before = graphPosition(event->position());
    zoomValue = std::clamp(zoomValue * (event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1), 0.35, 1.8);
    pan = event->position() - before * zoomValue;
    requestRender();
    event->accept();
}

void NodeGraphCanvasItem::keyPressEvent(QKeyEvent* event) {
    if (!event || !graphModel) return;
    if (event->key() == Qt::Key_Delete) graphModel->removeSelectedNodes();
    else if (event->matches(QKeySequence::Copy)) graphModel->copySelectedNodes();
    else if (event->matches(QKeySequence::Paste)) graphModel->pasteCopiedNodes();
    else { QQuickPaintedItem::keyPressEvent(event); return; }
    event->accept();
}

void NodeGraphCanvasItem::paint(QPainter* painter) {
    if (!painter) return;
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::LosslessImageRendering, true);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter->translate(pan);
    painter->scale(zoomValue, zoomValue);

    for (const Node& node : nodes) {
        if (isSimulationNode(node)) {
            paintSimulationHighlight(*painter, node);
        }
    }

    for (int edgeIndex = 0; edgeIndex < static_cast<int>(edges.size()); ++edgeIndex) {
        const Edge& edge = edges[edgeIndex];
        const Node* from = findNode(edge.fromNode);
        const Node* to = findNode(edge.toNode);
        if (!from || !to) continue;
        const QPointF start = socketPositionById(*from, true, edge.fromSocket);
        const QPointF end = socketPositionById(*to, false, edge.toSocket);
        QColor color = nodegraphscene::valueTypeColor(static_cast<NodeGraphValueType>(edge.valueType)).lighter(135);
        const bool hovered = edgeIndex == hoveredEdgeIndex;
        if (hovered) color = color.lighter(nodegraphscene::edgeHoverLightenFactor);
        QPen pen(color, hovered ? nodegraphscene::edgeHoveredWidth : nodegraphscene::edgeDefaultWidth,
                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(nodegraphscene::buildEdgePath(start, end));
    }

    if (interaction == Interaction::Connect) {
        if (const Node* source = findNode(interactionNodeId)) {
            const QPointF start = socketPositionById(*source, connectionFromOutput, interactionSocketId);
            const QPointF end = graphPosition(connectionEnd);
            const auto& sourceSockets = connectionFromOutput ? source->outputs : source->inputs;
            const Socket* sourceSocket = nullptr;
            for (const Socket& socket : sourceSockets) {
                if (socket.id == interactionSocketId) {
                    sourceSocket = &socket;
                    break;
                }
            }
            QColor previewColor = nodegraphscene::dragPreviewColor();
            if (sourceSocket && sourceSocket->valueType != 0) {
                previewColor = nodegraphscene::valueTypeColor(
                    static_cast<NodeGraphValueType>(sourceSocket->valueType)).lighter(135);
            }
            QPen pen(previewColor, nodegraphscene::edgeDefaultWidth,
                     Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(nodegraphscene::buildEdgePath(
                start, end,
                connectionFromOutput ? NodeGraphSocketDirection::Output : NodeGraphSocketDirection::Input,
                connectionFromOutput ? NodeGraphSocketDirection::Input : NodeGraphSocketDirection::Output));
        }
    }

    if (interaction == Interaction::BoxSelect) {
        const QRectF selection = QRectF(selectionStart, selectionEnd).normalized();
        painter->setPen(QPen(nodegraphscene::selectedBorderColor(), 1.0, Qt::SolidLine));
        painter->setBrush(QColor(94, 124, 255, 45));
        painter->drawRect(selection);
    }

    for (const Node& node : nodes) {
        painter->save();
        painter->translate(node.x, node.y);
        const QRectF shellRect = nodegraphscene::outerFrameRect();
        const QRectF centerRect = nodegraphscene::centerPanelRect();
        QPainterPath shellPath;
        shellPath.addRoundedRect(shellRect, nodegraphscene::roundCorners, nodegraphscene::roundCorners);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, nodegraphscene::nodeShadowAlpha));
        painter->drawRoundedRect(
            shellRect.translated(nodegraphscene::nodeShadowOffsetX, nodegraphscene::nodeShadowOffsetY),
            nodegraphscene::roundCorners, nodegraphscene::roundCorners);

        painter->setPen(Qt::NoPen);
        painter->setBrush(nodegraphscene::nodeShellColor());
        painter->drawPath(shellPath);
        painter->save();
        painter->setClipPath(shellPath);
        paintGraphCap(*painter, nodegraphscene::leftCapPath(),
                      nodegraphscene::frozenCapActiveColor(), nodegraphscene::frozenCapInactiveColor(),
                      node.frozen, node.id == hoveredNodeId && hoveredCap == 1);
        paintGraphCap(*painter, nodegraphscene::rightCapPath(),
                      nodegraphscene::displayCapActiveColor(), nodegraphscene::displayCapInactiveColor(),
                      node.displayEnabled, node.id == hoveredNodeId && hoveredCap == 2);
        QLinearGradient centerGradient(centerRect.topLeft(), centerRect.bottomLeft());
        centerGradient.setColorAt(0.0, nodegraphscene::nodeCenterFillColor());
        centerGradient.setColorAt(1.0, nodegraphscene::nodeCenterFillColor().darker(nodegraphscene::centerGradientDarken));
        painter->setPen(Qt::NoPen);
        painter->setBrush(centerGradient);
        painter->drawPath(nodegraphscene::centerPanelPath());
        painter->setPen(QPen(nodegraphscene::nodeShellOutlineColor(), nodegraphscene::capBorderWidth));
        painter->drawLine(QLineF(centerRect.topLeft(), centerRect.bottomLeft()));
        painter->drawLine(QLineF(centerRect.topRight(), centerRect.bottomRight()));
        painter->restore();

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(node.selected ? nodegraphscene::selectedBorderColor() : nodegraphscene::nodeShellOutlineColor(),
                             node.selected ? nodegraphscene::selectedBorderWidth : nodegraphscene::nodeOuterBorderWidth));
        painter->drawPath(shellPath);

        if (!node.icon.isNull()) {
            const QRectF bounds = nodegraphscene::iconRect();
            const QSizeF sourceSize = node.icon.size();
            const qreal scale = std::min(bounds.width() / sourceSize.width(), bounds.height() / sourceSize.height());
            const QSizeF size(sourceSize.width() * scale, sourceSize.height() * scale);
            const QRectF target(bounds.center().x() - size.width() * 0.5,
                                bounds.center().y() - size.height() * 0.5,
                                size.width(), size.height());
            painter->drawImage(target, node.icon);
        }

        const QString title = node.title.isEmpty() ? node.typeId : node.title;
        QFont titleFont = ui::UiTypography::font(ui::TextRole::NodeTitle);
        painter->setFont(titleFont);
        painter->setPen(nodegraphscene::titleColor());
        const QFontMetricsF metrics(titleFont);
        const QRectF titleBounds = metrics.boundingRect(title);
        const QPointF titlePosition = nodegraphscene::titlePosition(titleBounds);
        painter->drawText(QPointF(titlePosition.x(), titlePosition.y() + metrics.ascent()), title);

        const auto paintSockets = [this, painter, &node](bool output) {
            const auto& sockets = output ? node.outputs : node.inputs;
            for (int index = 0; index < static_cast<int>(sockets.size()); ++index) {
                const QPointF center = output
                    ? nodegraphscene::outputSocketPosition(static_cast<std::size_t>(index), sockets.size())
                    : nodegraphscene::inputSocketPosition(static_cast<std::size_t>(index), sockets.size());
                const bool hovered = node.id == hoveredSocketNodeId &&
                    sockets[index].id == hoveredSocketId && output == hoveredSocketOutput;
                QColor fill = nodegraphscene::valueTypeColor(static_cast<NodeGraphValueType>(sockets[index].valueType));
                if (hovered) fill = hoveredSocketCompatible ? fill.lighter(nodegraphscene::edgeHoverLightenFactor) : QColor(220, 75, 75);
                painter->setPen(QPen(hovered ? (hoveredSocketCompatible ? nodegraphscene::titleColor() : QColor(255, 120, 120)) : nodegraphscene::socketBorderColor(),
                                     hovered ? nodegraphscene::socketBorderWidth * 1.5 : nodegraphscene::socketBorderWidth));
                painter->setBrush(fill);
                const qreal radius = hovered ? nodegraphscene::socketRadius + 1.0 : nodegraphscene::socketRadius;
                painter->drawEllipse(center, radius, radius);
            }
        };
        paintSockets(false);
        paintSockets(true);
        painter->restore();
    }
}
