#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

class NodeGraphBridge;
class NodeGraphScene;
class NodeInspectorDialog;
class ModelRegistry;
class ModelSelection;
class RuntimeQuery;
class QGraphicsView;
class QLabel;
class QPointF;
class QPoint;

class NodeGraphDock : public QWidget {
public:
    explicit NodeGraphDock(QWidget* parent = nullptr);

    void setRuntimeQuery(const RuntimeQuery* runtimeQuery);
    void setBridge(NodeGraphBridge* bridge);
    void setModelRegistry(const ModelRegistry* modelRegistry);
    void setModelSelection(ModelSelection* modelSelection);
    void refreshGraph();
    void syncSelection();

private:
    void createUi();
    void handleGraphSelectionChanged(NodeGraphNodeId nodeId);
    void openInspectorForNode(NodeGraphNodeId nodeId);
    void syncViewportSelectionToGraph();
    void showCreateNodeMenu(const QPoint& globalPos, const QPointF& scenePos, bool requireEmptySpace);
    void addNodeAt(const NodeTypeId& typeId, const QPointF& scenePos);

    const RuntimeQuery* runtimeQuery = nullptr;
    NodeGraphBridge* bridge = nullptr;
    const ModelRegistry* modelRegistry = nullptr;
    ModelSelection* modelSelection = nullptr;
    NodeGraphScene* graphScene = nullptr;
    NodeInspectorDialog* inspectorDialog = nullptr;
    QGraphicsView* graphView = nullptr;
    QLabel* statusLabel = nullptr;
    bool suppressGraphSelectionHandling = false;
    uint32_t lastObservedRuntimeModelId = 0;
};
