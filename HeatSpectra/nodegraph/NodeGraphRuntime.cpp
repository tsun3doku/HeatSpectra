#include "NodeGraphRuntime.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphExecutionPlanner.hpp"
#include "NodeSolverController.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

NodeGraphRuntime::NodeGraphRuntime(NodeGraphBridge* nodeGraphBridge, const NodeRuntimeServices& services)
    : bridge(nodeGraphBridge),
      runtimeServices(services) {
}

NodeGraphRuntime::~NodeGraphRuntime() = default;

void NodeGraphRuntime::rebuildNodeById() {
    nodeById.clear();
    nodeById.reserve(graphState.nodes.size() * 2);
    for (const NodeGraphNode& node : graphState.nodes) {
        nodeById[node.id.value] = &node;
    }
}

void NodeGraphRuntime::applyDelta(const NodeGraphDelta& delta) {
    if (!delta.changes.empty()) {
        for (const NodeGraphChange& change : delta.changes) {
            applyChange(change);
        }
        rebuildNodeById();
    }
    graphState.revision = delta.toRevision;
}

void NodeGraphRuntime::applyChange(const NodeGraphChange& change) {
    switch (change.type) {
    case NodeGraphChangeType::Reset:
        graphState.nodes.clear();
        graphState.edges.clear();
        break;
    case NodeGraphChangeType::NodeUpsert: {
        auto nodeIt = std::find_if(
            graphState.nodes.begin(),
            graphState.nodes.end(),
            [&change](const NodeGraphNode& node) {
                return node.id == change.node.id;
            });
        if (nodeIt != graphState.nodes.end()) {
            *nodeIt = change.node;
        } else {
            graphState.nodes.push_back(change.node);
        }
        break;
    }
    case NodeGraphChangeType::NodeRemoved:
        graphState.nodes.erase(
            std::remove_if(
                graphState.nodes.begin(),
                graphState.nodes.end(),
                [&change](const NodeGraphNode& node) {
                    return node.id == change.nodeId;
                }),
            graphState.nodes.end());
        graphState.edges.erase(
            std::remove_if(
                graphState.edges.begin(),
                graphState.edges.end(),
                [&change](const NodeGraphEdge& edge) {
                    return edge.fromNode == change.nodeId || edge.toNode == change.nodeId;
                }),
            graphState.edges.end());
        break;
    case NodeGraphChangeType::EdgeUpsert: {
        auto edgeIt = std::find_if(
            graphState.edges.begin(),
            graphState.edges.end(),
            [&change](const NodeGraphEdge& edge) {
                return edge.id == change.edge.id;
            });
        if (edgeIt != graphState.edges.end()) {
            *edgeIt = change.edge;
        } else {
            graphState.edges.push_back(change.edge);
        }
        break;
    }
    case NodeGraphChangeType::EdgeRemoved:
        graphState.edges.erase(
            std::remove_if(
                graphState.edges.begin(),
                graphState.edges.end(),
                [&change](const NodeGraphEdge& edge) {
                    return edge.id == change.edgeId;
                }),
            graphState.edges.end());
        break;
    }
}

void NodeGraphRuntime::tick(NodeGraphRuntimeExecutionState* outState) {
    if (!bridge) {
        if (outState) {
            outState->sourceSocketByInputSocket.clear();
            outState->outputValueBySocket.clear();
        }
        return;
    }

    executeDataflow(outState);

    bool hasHeatSolveNode = false;
    for (const NodeGraphNode& node : graphState.nodes) {
        if (canonicalNodeTypeId(node.typeId) == nodegraphtypes::HeatSolve) {
            hasHeatSolveNode = true;
            break;
        }
    }
    if (!hasHeatSolveNode && runtimeServices.nodeSolverController) {
        runtimeServices.nodeSolverController->deactivateHeatSolveIfActive();
    }
}

void NodeGraphRuntime::executeDataflow(NodeGraphRuntimeExecutionState* outState) {
    std::vector<NodeGraphNodeId> executionOrder;
    if (!NodeGraphExecutionPlanner::buildTopologicalOrder(graphState, executionOrder, nullptr)) {
        if (runtimeServices.nodeSolverController) {
            runtimeServices.nodeSolverController->deactivateHeatSolveIfActive();
        }
        if (outState) {
            outState->sourceSocketByInputSocket.clear();
            outState->outputValueBySocket.clear();
        }
        return;
    }

    std::unordered_map<uint64_t, const NodeGraphEdge*> incomingEdgeByInputSocket;
    incomingEdgeByInputSocket.reserve(graphState.edges.size() * 2);
    NodeGraphRuntimeExecutionState state{};
    state.sourceSocketByInputSocket.reserve(graphState.edges.size() * 2);
    state.outputValueBySocket.reserve(graphState.edges.size() * 2);
    for (const NodeGraphEdge& edge : graphState.edges) {
        const uint64_t inputKey = makeSocketKey(edge.toNode, edge.toSocket);
        incomingEdgeByInputSocket[inputKey] = &edge;
        state.sourceSocketByInputSocket[inputKey] = makeSocketKey(edge.fromNode, edge.fromSocket);
    }

    for (NodeGraphNodeId nodeId : executionOrder) {
        const auto nodeIt = nodeById.find(nodeId.value);
        if (nodeIt == nodeById.end() || !nodeIt->second) {
            continue;
        }

        const NodeGraphNode& node = *nodeIt->second;
        const NodeTypeId typeId = canonicalNodeTypeId(node.typeId);

        std::vector<const NodeDataBlock*> inputValues(node.inputs.size(), nullptr);
        for (std::size_t inputIndex = 0; inputIndex < node.inputs.size(); ++inputIndex) {
            const NodeGraphSocket& inputSocket = node.inputs[inputIndex];
            const auto edgeIt = incomingEdgeByInputSocket.find(makeSocketKey(node.id, inputSocket.id));
            if (edgeIt == incomingEdgeByInputSocket.end() || !edgeIt->second) {
                continue;
            }

            const NodeGraphEdge& edge = *edgeIt->second;
            const auto outputIt = state.outputValueBySocket.find(makeSocketKey(edge.fromNode, edge.fromSocket));
            if (outputIt == state.outputValueBySocket.end()) {
                continue;
            }

            inputValues[inputIndex] = &outputIt->second;
        }

        std::vector<NodeDataBlock> outputValues;
        outputValues.reserve(node.outputs.size());
        if (kernels.hasKernel(typeId)) {
            outputValues.resize(node.outputs.size());
            seedOutputDataBlocksFromInputs(node, inputValues, outputValues);

            const NodeGraphKernelExecutionState kernelState{
                graphState,
                *bridge,
                runtimeServices,
                incomingEdgeByInputSocket,
                state.outputValueBySocket};

            kernels.executeNode(node, kernelState, inputValues, outputValues);

            for (std::size_t outputIndex = 0; outputIndex < node.outputs.size(); ++outputIndex) {
                state.outputValueBySocket[makeSocketKey(node.id, node.outputs[outputIndex].id)] =
                    outputValues[outputIndex];
            }
        }
    }

    if (outState) {
        outState->sourceSocketByInputSocket = std::move(state.sourceSocketByInputSocket);
        outState->outputValueBySocket = std::move(state.outputValueBySocket);
    }
}

uint64_t NodeGraphRuntime::makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}
