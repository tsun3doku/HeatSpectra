#include "GraphHost.hpp"

#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeHeatSolveParams.hpp"
#include "runtime/TimelineRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <QDebug>

GraphHost::GraphHost(QObject* parent)
    : QObject(parent), editor(graph) {
}

void GraphHost::initialize() {
    if (started) return;
    publishedRevision = graph.getRevision();
    const bool pythonReady = python.initialize(graph);
    started = true;
    emit initialized(
        graph.state(),
        graph.getRegistry().allNodeTypes(),
        pythonReady ? QString::fromStdString(python.pythonVersion()) : QStringLiteral("unavailable"));
    publishTimelineRange();
}

void GraphHost::shutdown() {
    if (started) {
        python.shutdown();
        started = false;
    }
}

void GraphHost::newProject() {
    editor.resetToDefaultGraph();
    publishedRevision = graph.getRevision();
    emit graphReplaced(graph.state());
    publishTimelineRange();
}

void GraphHost::newEmptyProject() {
    graph.clear();
    publishedRevision = graph.getRevision();
    emit graphReplaced(graph.state());
    publishTimelineRange();
}

void GraphHost::requestGraphState() {
    emit graphStateReady(graph.state());
}

void GraphHost::loadGraphState(const NodeGraphState& state) {
    std::string error;
    if (!graph.loadSerializedState(state, state.nextNodeId, state.nextSocketId,
                                  state.nextEdgeId, error)) {
        emit graphStateLoaded(false, QString::fromStdString(error));
        return;
    }
    publishedRevision = graph.getRevision();
    emit graphReplaced(graph.state());
    publishTimelineRange();
    emit graphStateLoaded(true, {});
}

void GraphHost::resetGraph() {
    editor.resetToDefaultGraph();
    publishChanges();
}

void GraphHost::replaceGraph(const NodeGraphState& state) {
    uint32_t nextNodeId = 1;
    uint32_t nextSocketId = 1;
    uint32_t nextEdgeId = 1;
    for (const auto& entry : state.nodes) {
        nextNodeId = std::max(nextNodeId, entry.first + 1);
        for (const NodeGraphSocket& socket : entry.second.inputs) nextSocketId = std::max(nextSocketId, socket.id.value + 1);
        for (const NodeGraphSocket& socket : entry.second.outputs) nextSocketId = std::max(nextSocketId, socket.id.value + 1);
    }
    for (const auto& entry : state.edges) nextEdgeId = std::max(nextEdgeId, entry.first + 1);

    std::string error;
    if (!graph.loadSerializedState(state, nextNodeId, nextSocketId, nextEdgeId, error)) {
        return;
    }
    publishedRevision = graph.getRevision();
    emit graphReplaced(graph.state());
    publishTimelineRange();
}

void GraphHost::addNode(const QString& typeId, float x, float y) {
    if (!editor.addNode(typeId.toStdString(), {}, x, y).isValid()) {
        return;
    }
    publishChanges();
}

void GraphHost::removeNode(NodeGraphNodeId nodeId) {
    if (!editor.removeNode(nodeId)) return;
    publishChanges();
}

void GraphHost::moveNode(NodeGraphNodeId nodeId, float x, float y) {
    if (!editor.moveNode(nodeId, x, y)) return;
    publishChanges();
}

void GraphHost::toggleNodeFrozen(NodeGraphNodeId nodeId) {
    if (!editor.toggleNodeFrozen(nodeId)) return;
    publishChanges();
}

void GraphHost::toggleNodeDisplay(NodeGraphNodeId nodeId) {
    if (!editor.toggleNodeDisplay(nodeId)) return;
    publishChanges();
}

void GraphHost::connectSockets(NodeGraphNodeId fromNode, NodeGraphSocketId fromSocket,
                               NodeGraphNodeId toNode, NodeGraphSocketId toSocket) {
    std::string error;
    if (!editor.connectSockets(fromNode, fromSocket, toNode, toSocket, error, true)) {
        qWarning().noquote() << QStringLiteral("Node graph connection failed: %1")
                                    .arg(QString::fromStdString(error));
        return;
    }
    publishChanges();
}

void GraphHost::removeConnection(NodeGraphEdgeId edgeId) {
    if (!editor.removeConnection(edgeId)) return;
    publishChanges();
}

void GraphHost::setParameter(NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    setParameters(nodeId, std::vector<NodeGraphParamValue>{parameter});
}

void GraphHost::setParameters(
    NodeGraphNodeId nodeId,
    const std::vector<NodeGraphParamValue>& parameters) {
    if (!editor.setNodeParameters(nodeId, parameters)) return;
    publishChanges();
}

void GraphHost::pasteFragment(const GraphPastePayload& payload) {
    std::vector<NodeGraphNodeId> created;
    if (!editor.pasteCopiedNodes(payload.nodes, payload.edges, payload.offset, created)) return;
    publishChanges();
    emit nodesPasted(created);
}

void GraphHost::executePython(const QString& source) {
    PythonResult result{};
    if (!python.isInitialized()) {
        result.error = QStringLiteral("Python interpreter is unavailable.\n");
        emit pythonFinished(result);
        return;
    }
    result.incomplete = python.runSource(source.toStdString());
    result.output = QString::fromStdString(python.consumeOutput());
    result.error = QString::fromStdString(python.consumeError());
    publishChanges();
    emit pythonFinished(result);
}

void GraphHost::activateTimeline() {
    const NodeGraphNodeId nodeId = timelineHeatSolveNode();
    if (!nodeId.isValid()) return;
    if (editor.setNodeParameter(nodeId, NodeGraphParamValue{
            nodegraphparams::heatsolve::Enabled, NodeGraphParamType::Bool, 0.0, 0, true})) {
        publishChanges();
    }
}

void GraphHost::publishChanges() {
    NodeGraphDelta delta{};
    if (graph.consumeChanges(publishedRevision, delta)) {
        emit graphChanged(delta);
    }
    publishTimelineRange();
}

void GraphHost::publishTimelineRange() {
    uint32_t frameCount = TimelineRuntime::DefaultFrameCount;
    const NodeGraphNodeId nodeId = timelineHeatSolveNode();
    NodeGraphNode node{};
    if (nodeId.isValid() && graph.getNode(nodeId, node)) {
        const double duration = std::max(0.0, readHeatSolveNodeParams(node).simulationDuration);
        const uint32_t maxFrame = duration > 0.0
            ? std::max(1u, static_cast<uint32_t>(std::ceil(duration * TimelineRuntime::DefaultFps)))
            : 0u;
        frameCount = maxFrame + 1u;
    }

    if (frameCount == publishedTimelineFrameCount) return;
    publishedTimelineFrameCount = frameCount;
    emit timelineRangeChanged(frameCount, TimelineRuntime::DefaultFps);
}

NodeGraphNodeId GraphHost::timelineHeatSolveNode() const {
    NodeGraphNodeId result{};
    const NodeGraphState state = graph.state();
    for (const auto& entry : state.nodes) {
        const NodeGraphNode& node = entry.second;
        if (getNodeTypeId(node.typeId) != nodegraphtypes::HeatSolve) continue;
        if (result.isValid()) return {};
        result = node.id;
    }
    return result;
}
