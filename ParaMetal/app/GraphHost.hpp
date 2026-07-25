#pragma once

#include "UiRuntimeTypes.hpp"
#include "nodegraph/NodeGraph.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "py/PyInterpreter.hpp"

#include <QObject>
#include <QString>

class GraphHost final : public QObject {
    Q_OBJECT

public:
    explicit GraphHost(QObject* parent = nullptr);

public slots:
    void initialize();
    void shutdown();
    void newProject();
    void resetGraph();
    void replaceGraph(const NodeGraphState& state);
    void addNode(const QString& typeId, float x, float y);
    void removeNode(NodeGraphNodeId nodeId);
    void moveNode(NodeGraphNodeId nodeId, float x, float y);
    void toggleNodeFrozen(NodeGraphNodeId nodeId);
    void toggleNodeDisplay(NodeGraphNodeId nodeId);
    void connectSockets(NodeGraphNodeId fromNode, NodeGraphSocketId fromSocket, NodeGraphNodeId toNode, NodeGraphSocketId toSocket);
    void removeConnection(NodeGraphEdgeId edgeId);
    void setParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter);
    void setParameters(NodeGraphNodeId nodeId, const std::vector<NodeGraphParamValue>& parameters);
    void pasteFragment(const GraphPastePayload& payload);
    void executePython(const QString& source);
    void activateTimeline();
    void requestGraphState();
    void loadGraphState(const NodeGraphState& state);

signals:
    void initialized(const NodeGraphState& state,const std::vector<NodeTypeDefinition>& definitions, const QString& pythonVersion);
    void graphChanged(const NodeGraphDelta& delta);
    void graphReplaced(const NodeGraphState& state);
    void nodesPasted(const std::vector<NodeGraphNodeId>& nodeIds);
    void pythonFinished(const PythonResult& result);
    void timelineRangeChanged(uint32_t frameCount, float fps);
    void graphStateReady(const NodeGraphState& state);
    void graphStateLoaded(bool success, const QString& error);

private:
    void publishChanges();
    void publishTimelineRange();
    NodeGraphNodeId timelineHeatSolveNode() const;

    NodeGraph graph;
    NodeGraphEditor editor;
    PyInterpreter python;
    uint64_t publishedRevision = 0;
    uint32_t publishedTimelineFrameCount = 0;
    bool started = false;
};
