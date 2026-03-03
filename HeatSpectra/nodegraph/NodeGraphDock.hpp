#pragma once

#include "NodeGraphTypes.hpp"

#include <QWidget>

class NodeGraphBridge;
class NodeGraphScene;
class NodeInspectorDialog;
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
    void refreshGraph();

private:
    void createUi();
    void openInspectorForNode(NodeGraphNodeId nodeId);
    void showCreateNodeMenu(const QPoint& globalPos, const QPointF& scenePos, bool requireEmptySpace);
    void addNodeAt(const NodeTypeId& typeId, const QPointF& scenePos);

    const RuntimeQuery* runtimeQuery = nullptr;
    NodeGraphBridge* bridge = nullptr;
    NodeGraphScene* graphScene = nullptr;
    NodeInspectorDialog* inspectorDialog = nullptr;
    QGraphicsView* graphView = nullptr;
    QLabel* statusLabel = nullptr;
};
