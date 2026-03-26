#pragma once

#include "NodeGraphEditor.hpp"
#include "NodeGraphTypes.hpp"

#include <QWidget>

class NodeGraphBridge;
class NodeGraphScene;
class NodeInspectorDialog;
class ModelSelection;
class RuntimeQuery;
class SceneController;
class QGraphicsView;
class QLabel;
class QPointF;
class QPoint;

class NodeGraphDock : public QWidget {
public:
    explicit NodeGraphDock(QWidget* parent = nullptr);

    void setRuntimeQuery(const RuntimeQuery* runtimeQuery);
    void setBridge(NodeGraphBridge* bridge);
    void setSceneController(const SceneController* sceneController);
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
    NodeGraphEditor editor;
    const SceneController* sceneController = nullptr;
    ModelSelection* modelSelection = nullptr;
    NodeGraphScene* graphScene = nullptr;
    NodeInspectorDialog* inspectorDialog = nullptr;
    QGraphicsView* graphView = nullptr;
    QLabel* statusLabel = nullptr;
    bool suppressGraphSelectionHandling = false;
    uint32_t lastObservedRuntimeModelId = 0;
};
