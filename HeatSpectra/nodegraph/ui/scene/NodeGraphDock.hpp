#pragma once

#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeGraphTypes.hpp"

#include <QWidget>

class NodeGraphBridge;
class NodeGraphScene;
class NodePanel;
class ModelSelection;
class RuntimeQuery;
class SceneController;
class QGraphicsView;
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
    NodePanel* nodePanel = nullptr;
    QGraphicsView* graphView = nullptr;
    bool suppressGraphSelectionHandling = false;
    uint32_t lastObservedRuntimeModelId = 0;
};
