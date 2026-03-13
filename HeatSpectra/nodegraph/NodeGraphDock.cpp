#include "NodeGraphDock.hpp"

#include "NodeInspectorDialog.hpp"
#include "NodeGraphBridge.hpp"
#include "NodeGraphScene.hpp"
#include "runtime/RuntimeInterfaces.hpp"
#include "scene/ModelRegistry.hpp"
#include "scene/ModelSelection.hpp"
#include "util/UiTheme.hpp"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSplitter>
#include <QString>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QPoint>

#include <functional>
#include <limits>
#include <map>
#include <utility>

namespace {

class NodeGraphView : public QGraphicsView {
public:
    explicit NodeGraphView(QWidget* parent = nullptr)
        : QGraphicsView(parent) {
        setDragMode(QGraphicsView::RubberBandDrag);
        setRubberBandSelectionMode(Qt::IntersectsItemShape);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setCursor(Qt::ArrowCursor);
        viewport()->setCursor(Qt::ArrowCursor);
        setRenderHint(QPainter::Antialiasing, true);
        setRenderHint(QPainter::TextAntialiasing, true);
    }

    void setOpenCreateMenuCallback(std::function<void(const QPoint&, const QPointF&, bool)> callback) {
        openCreateMenuCallback = std::move(callback);
    }

    void setDeleteSelectedCallback(std::function<void()> callback) {
        deleteSelectedCallback = std::move(callback);
    }

    void setCopySelectedCallback(std::function<void()> callback) {
        copySelectedCallback = std::move(callback);
    }

    void setPasteCallback(std::function<void()> callback) {
        pasteCallback = std::move(callback);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event && event->button() == Qt::MiddleButton) {
            isPanningWithMiddleMouse = true;
            lastPanPoint = event->pos();
            event->accept();
            return;
        }

        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (event && isPanningWithMiddleMouse) {
            const QPoint delta = event->pos() - lastPanPoint;
            lastPanPoint = event->pos();

            if (horizontalScrollBar()) {
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            }
            if (verticalScrollBar()) {
                verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            }
            event->accept();
            return;
        }

        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event && event->button() == Qt::MiddleButton) {
            isPanningWithMiddleMouse = false;
            event->accept();
            return;
        }

        QGraphicsView::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        constexpr qreal zoomInFactor = 1.15;
        constexpr qreal zoomOutFactor = 1.0 / zoomInFactor;
        if (event->angleDelta().y() > 0) {
            scale(zoomInFactor, zoomInFactor);
        } else if (event->angleDelta().y() < 0) {
            scale(zoomOutFactor, zoomOutFactor);
        }
        event->accept();
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (!event) {
            return;
        }

        if (openCreateMenuCallback) {
            openCreateMenuCallback(event->globalPos(), mapToScene(event->pos()), true);
            event->accept();
            return;
        }

        QGraphicsView::contextMenuEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!event) {
            return;
        }

        if (event->matches(QKeySequence::Copy)) {
            if (copySelectedCallback) {
                copySelectedCallback();
                event->accept();
                return;
            }
        }

        if (event->matches(QKeySequence::Paste)) {
            if (pasteCallback) {
                pasteCallback();
                event->accept();
                return;
            }
        }

        if (event->key() == Qt::Key_Delete) {
            if (deleteSelectedCallback) {
                deleteSelectedCallback();
                event->accept();
                return;
            }
        }

        if (event->key() == Qt::Key_Tab) {
            if (openCreateMenuCallback) {
                QPoint anchorViewPos = mapFromGlobal(QCursor::pos());
                if (!viewport()->rect().contains(anchorViewPos)) {
                    anchorViewPos = viewport()->rect().center();
                }

                openCreateMenuCallback(viewport()->mapToGlobal(anchorViewPos), mapToScene(anchorViewPos), false);
                event->accept();
                return;
            }
        }

        QGraphicsView::keyPressEvent(event);
    }

private:
    std::function<void(const QPoint&, const QPointF&, bool)> openCreateMenuCallback;
    std::function<void()> deleteSelectedCallback;
    std::function<void()> copySelectedCallback;
    std::function<void()> pasteCallback;
    bool isPanningWithMiddleMouse = false;
    QPoint lastPanPoint{};
};

QString nodeCategoryDisplayName(NodeGraphNodeCategory category) {
    switch (category) {
    case NodeGraphNodeCategory::Model:
        return "Model";
    case NodeGraphNodeCategory::PointSurface:
        return "Point/Surface";
    case NodeGraphNodeCategory::Meshing:
        return "Meshing";
    case NodeGraphNodeCategory::System:
        return "System";
    case NodeGraphNodeCategory::Custom:
        return "Misc";
    }

    return "Misc";
}

int nodeCategorySortKey(NodeGraphNodeCategory category) {
    switch (category) {
    case NodeGraphNodeCategory::Model:
        return 0;
    case NodeGraphNodeCategory::PointSurface:
        return 1;
    case NodeGraphNodeCategory::Meshing:
        return 2;
    case NodeGraphNodeCategory::System:
        return 3;
    case NodeGraphNodeCategory::Custom:
        return 4;
    }

    return 4;
}

}

NodeGraphDock::NodeGraphDock(QWidget* parent)
    : QWidget(parent) {
    createUi();
}

void NodeGraphDock::setRuntimeQuery(const RuntimeQuery* runtimeQueryPtr) {
    if (runtimeQuery == runtimeQueryPtr) {
        return;
    }

    runtimeQuery = runtimeQueryPtr;
    if (inspectorDialog) {
        inspectorDialog->bind(bridge, runtimeQuery);
    }
}

void NodeGraphDock::setBridge(NodeGraphBridge* bridgePtr) {
    bridge = bridgePtr;
    if (graphScene) {
        graphScene->setBridge(bridge);
    }
    if (inspectorDialog) {
        inspectorDialog->bind(bridge, runtimeQuery);
    }

    if (statusLabel) {
        statusLabel->setText(bridge
            ? "Graph ready. Right-click empty space or press Tab to add nodes. Delete removes selected nodes. Ctrl+C/Ctrl+V copies and pastes selected nodes."
            : "Waiting for runtime graph bridge...");
    }
}

void NodeGraphDock::setModelRegistry(const ModelRegistry* modelRegistryPtr) {
    modelRegistry = modelRegistryPtr;
}

void NodeGraphDock::setModelSelection(ModelSelection* modelSelectionPtr) {
    modelSelection = modelSelectionPtr;
    lastObservedRuntimeModelId = std::numeric_limits<uint32_t>::max();
}

void NodeGraphDock::refreshGraph() {
    if (graphScene) {
        graphScene->refreshFromGraph();
    }
}

void NodeGraphDock::syncSelection() {
    syncViewportSelectionToGraph();
}

void NodeGraphDock::createUi() {
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    graphScene = new NodeGraphScene(this);
    NodeGraphView* nodeGraphView = new NodeGraphView(this);
    graphView = nodeGraphView;
    graphView->setScene(graphScene);
    graphView->setFocusPolicy(Qt::StrongFocus);

    inspectorDialog = new NodeInspectorDialog(this);
    inspectorDialog->bind(bridge, runtimeQuery);

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);
    ui::configureSplitter(*splitter);
    splitter->addWidget(inspectorDialog);
    splitter->addWidget(graphView);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setSizes({260, 500});
    rootLayout->addWidget(splitter, 1);

    graphScene->setNodeActivatedCallback([this](NodeGraphNodeId nodeId, const QPointF&) {
        openInspectorForNode(nodeId);
    });
    graphScene->setNodeSelectionChangedCallback([this](NodeGraphNodeId nodeId) {
        handleGraphSelectionChanged(nodeId);
    });
    graphScene->setStatusCallback([this](const QString& text) {
        if (statusLabel && !text.isEmpty()) {
            statusLabel->setText(text);
        }
    });
    nodeGraphView->setOpenCreateMenuCallback([this](const QPoint& globalPos, const QPointF& scenePos, bool requireEmptySpace) {
        showCreateNodeMenu(globalPos, scenePos, requireEmptySpace);
    });
    nodeGraphView->setDeleteSelectedCallback([this]() {
        if (!graphScene) {
            return;
        }

        if (graphScene->removeSelectedNodes()) {
            statusLabel->setText("Selected nodes removed.");
        } else {
            statusLabel->setText("No selected nodes were removed.");
        }
    });
    nodeGraphView->setCopySelectedCallback([this]() {
        if (!graphScene || !statusLabel) {
            return;
        }

        if (graphScene->copySelectedNodes()) {
            statusLabel->setText("Copied selected node(s).");
        } else {
            statusLabel->setText("Select at least one node to copy.");
        }
    });
    nodeGraphView->setPasteCallback([this]() {
        if (!graphScene || !statusLabel) {
            return;
        }

        if (graphScene->pasteCopiedNodes()) {
            statusLabel->setText("Pasted node copy.");
        } else {
            statusLabel->setText("Clipboard is empty. Copy nodes first.");
        }
    });

    statusLabel = new QLabel("Right-click empty space or press Tab to add nodes. Press Delete to remove selected nodes. Ctrl+C/Ctrl+V copies and pastes selected nodes.", this);
    rootLayout->addWidget(statusLabel);
}

void NodeGraphDock::handleGraphSelectionChanged(NodeGraphNodeId nodeId) {
    if (suppressGraphSelectionHandling) {
        return;
    }

    uint32_t runtimeModelId = 0;
    if (bridge && modelRegistry && nodeId.isValid()) {
        NodeGraphNode node{};
        if (bridge->getNode(nodeId, node) && canonicalNodeTypeId(node.typeId) == nodegraphtypes::Model) {
            modelRegistry->tryGetNodeModelRuntimeId(nodeId.value, runtimeModelId);
        }
    }

    if (modelSelection) {
        if (runtimeModelId != 0) {
            modelSelection->setSelectedModelID(runtimeModelId);
        } else {
            modelSelection->clearSelection();
        }
    }

    lastObservedRuntimeModelId = runtimeModelId;
}

void NodeGraphDock::openInspectorForNode(NodeGraphNodeId nodeId) {
    if (!inspectorDialog || !graphView) {
        return;
    }

    inspectorDialog->bind(bridge, runtimeQuery);
    if (!inspectorDialog->setNode(nodeId)) {
        statusLabel->setText("Failed to open node inspector.");
        return;
    }

    graphView->setFocus(Qt::OtherFocusReason);
}

void NodeGraphDock::syncViewportSelectionToGraph() {
    if (!graphScene || !modelSelection || !modelRegistry) {
        return;
    }

    const uint32_t selectedRuntimeModelId = modelSelection->getSelectedModelID();
    if (selectedRuntimeModelId == lastObservedRuntimeModelId) {
        return;
    }

    lastObservedRuntimeModelId = selectedRuntimeModelId;

    NodeGraphNodeId nodeId{};
    if (selectedRuntimeModelId != 0) {
        uint32_t nodeModelId = 0;
        if (modelRegistry->tryGetRuntimeModelNodeId(selectedRuntimeModelId, nodeModelId)) {
            nodeId = NodeGraphNodeId{nodeModelId};
        }
    }

    suppressGraphSelectionHandling = true;
    if (nodeId.isValid()) {
        graphScene->setSelectedNode(nodeId);
    } else {
        graphScene->clearNodeSelection();
    }
    suppressGraphSelectionHandling = false;

    if (nodeId.isValid()) {
        openInspectorForNode(nodeId);
    }
}

void NodeGraphDock::showCreateNodeMenu(const QPoint& globalPos, const QPointF& scenePos, bool requireEmptySpace) {
    if (!bridge || !graphScene) {
        if (statusLabel) {
            statusLabel->setText("Bridge unavailable.");
        }
        return;
    }

    if (requireEmptySpace) {
        const QGraphicsItem* hitItem = graphScene->itemAt(scenePos, QTransform());
        if (hitItem) {
            return;
        }
    }

    QMenu addMenu(this);
    std::map<int, QMenu*> categoryMenus;
    auto getCategoryMenu = [&](NodeGraphNodeCategory category) -> QMenu* {
        const int key = nodeCategorySortKey(category);
        const auto existing = categoryMenus.find(key);
        if (existing != categoryMenus.end()) {
            return existing->second;
        }

        QMenu* menu = addMenu.addMenu(nodeCategoryDisplayName(category));
        categoryMenus.emplace(key, menu);
        return menu;
    };

    const std::vector<NodeTypeDefinition>& definitions = builtInNodeTypeDefinitions();
    for (const NodeTypeDefinition& definition : definitions) {
        QMenu* categoryMenu = getCategoryMenu(definition.category);
        QAction* action = categoryMenu->addAction(QString::fromStdString(definition.displayName));
        action->setData(QString::fromStdString(definition.id));
    }

    QAction* selectedAction = addMenu.exec(globalPos);
    if (!selectedAction) {
        return;
    }

    addNodeAt(selectedAction->data().toString().toStdString(), scenePos);
}

void NodeGraphDock::addNodeAt(const NodeTypeId& typeId, const QPointF& scenePos) {
    if (!bridge || !graphScene) {
        if (statusLabel) {
            statusLabel->setText("Bridge unavailable.");
        }
        return;
    }

    bridge->addNode(typeId, "", static_cast<float>(scenePos.x()), static_cast<float>(scenePos.y()));
    graphScene->refreshFromGraph();
    if (statusLabel) {
        statusLabel->setText("Node added.");
    }
}
