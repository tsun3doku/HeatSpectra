#include "NodeGraphRuntime.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphCompiler.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "contact/ContactSystemController.hpp"
#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

NodeGraphRuntime::NodeGraphRuntime(NodeGraphBridge* nodeGraphBridge, const NodeRuntimeServices& services)
    : bridge(nodeGraphBridge),
      runtimeServices(services) {
}

NodeGraphRuntime::~NodeGraphRuntime() = default;

EvaluatedSocketValue NodeGraphRuntime::makeMissingSocketValue() const {
    EvaluatedSocketValue value{};
    value.status = EvaluatedSocketStatus::Missing;
    return value;
}

EvaluatedSocketValue NodeGraphRuntime::makeErrorSocketValue(std::string error) const {
    EvaluatedSocketValue value{};
    value.status = EvaluatedSocketStatus::Error;
    value.error = std::move(error);
    return value;
}

EvaluatedSocketValue NodeGraphRuntime::makeValueSocketValue(const NodeDataBlock& data) const {
    EvaluatedSocketValue value{};
    value.status = EvaluatedSocketStatus::Value;
    value.data = data;
    return value;
}

void NodeGraphRuntime::propagateSkippedNodeOutputs(
    const NodeGraphNode& node,
    EvaluatedSocketStatus status,
    const std::string& error,
    NodeGraphEvaluationState& state) const {
    const EvaluatedSocketValue skippedValue =
        (status == EvaluatedSocketStatus::Error)
        ? makeErrorSocketValue(error)
        : makeMissingSocketValue();
    for (const NodeGraphSocket& outputSocket : node.outputs) {
        state.outputBySocket[makeSocketKey(node.id, outputSocket.id)] = skippedValue;
    }
}

bool NodeGraphRuntime::evaluateNodeInputs(
    const NodeGraphNode& node,
    const std::unordered_map<uint64_t, const NodeGraphEdge*>& incomingEdgeByInputSocket,
    const NodeGraphEvaluationState& state,
    std::vector<const EvaluatedSocketValue*>& outInputs,
    EvaluatedSocketStatus& outStatus,
    std::string& outError) const {
    outInputs.assign(node.inputs.size(), nullptr);
    outStatus = EvaluatedSocketStatus::Value;
    outError.clear();

    for (std::size_t inputIndex = 0; inputIndex < node.inputs.size(); ++inputIndex) {
        const NodeGraphSocket& inputSocket = node.inputs[inputIndex];
        const auto edgeIt = incomingEdgeByInputSocket.find(makeSocketKey(node.id, inputSocket.id));
        if (edgeIt == incomingEdgeByInputSocket.end() || !edgeIt->second) {
            outStatus = EvaluatedSocketStatus::Missing;
            return false;
        }

        const NodeGraphEdge& edge = *edgeIt->second;
        const auto outputIt = state.outputBySocket.find(makeSocketKey(edge.fromNode, edge.fromSocket));
        if (outputIt == state.outputBySocket.end()) {
            outStatus = EvaluatedSocketStatus::Missing;
            return false;
        }

        const EvaluatedSocketValue& inputValue = outputIt->second;
        outInputs[inputIndex] = &inputValue;
        if (inputValue.status == EvaluatedSocketStatus::Value) {
            continue;
        }

        outStatus = inputValue.status;
        outError = inputValue.error;
        return false;
    }

    return true;
}

void NodeGraphRuntime::rebuildNodeById() {
    nodeById.clear();
    nodeById.reserve(graphState.nodes.size() * 2);
    for (const NodeGraphNode& node : graphState.nodes) {
        nodeById[node.id.value] = &node;
    }
}

void NodeGraphRuntime::applyDelta(const NodeGraphDelta& delta) {
    if (!delta.changes.empty()) {
        bool shouldClearCaches = false;
        std::unordered_set<uint32_t> dirtyNodeIds;
        for (const NodeGraphChange& change : delta.changes) {
            if (change.reason == NodeGraphChangeReason::Topology) {
                shouldClearCaches = true;
            }
            if (change.reason == NodeGraphChangeReason::Parameter &&
                change.type == NodeGraphChangeType::NodeUpsert &&
                change.node.id.isValid()) {
                dirtyNodeIds.insert(change.node.id.value);
            }
            applyChange(change);
        }
        rebuildNodeById();
        if (shouldClearCaches) {
            clearNodeCaches();
            if (runtimeServices.contactSystemController) {
                runtimeServices.contactSystemController->clearCache();
            }
            if (runtimeServices.contactPreviewStore) {
                runtimeServices.contactPreviewStore->clearAllPreviews();
            }
            if (runtimeServices.payloadRegistry) {
                runtimeServices.payloadRegistry->clear();
            }
        } else if (!dirtyNodeIds.empty()) {
            invalidateNodeCaches(dirtyNodeIds);
        }
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

void NodeGraphRuntime::tick(NodeGraphEvaluationState* outState) {
    if (!bridge) {
        if (outState) {
            outState->sourceSocketByInputSocket.clear();
            outState->outputBySocket.clear();
        }
        return;
    }

    executeDataflow(outState);

}

void NodeGraphRuntime::executeDataflow(NodeGraphEvaluationState* outState) {
    NodeGraphCompiled compiled = NodeGraphCompiler::compile(graphState);
    if (graphState.nodes.size() > 0 && compiled.executionOrder.size() != graphState.nodes.size()) {
        if (runtimeServices.contactPreviewStore) {
            runtimeServices.contactPreviewStore->endFrame();
        }
        if (outState) {
            outState->sourceSocketByInputSocket.clear();
            outState->outputBySocket.clear();
        }
        return;
    }

    const std::vector<NodeGraphNodeId>& executionOrder = compiled.executionOrder;
    std::unordered_map<uint64_t, const NodeGraphEdge*> incomingEdgeByInputSocket;
    incomingEdgeByInputSocket.reserve(graphState.edges.size() * 2);
    NodeGraphEvaluationState state{};
    state.sourceSocketByInputSocket.reserve(graphState.edges.size() * 2);
    state.outputBySocket.reserve(graphState.edges.size() * 2);
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
        const NodeTypeId typeId = getNodeTypeId(node.typeId);

        std::vector<const EvaluatedSocketValue*> inputValues;
        inputValues.reserve(node.inputs.size());
        EvaluatedSocketStatus blockedStatus = EvaluatedSocketStatus::Value;
        std::string blockedError;
        if (!evaluateNodeInputs(
                node,
                incomingEdgeByInputSocket,
                state,
                inputValues,
                blockedStatus,
                blockedError)) {
            propagateSkippedNodeOutputs(node, blockedStatus, blockedError, state);
            continue;
        }

        std::vector<NodeDataBlock> outputValues;
        outputValues.reserve(node.outputs.size());
        if (kernels.hasKernel(typeId)) {
            const NodeGraphKernelExecutionState kernelState{
                graphState,
                *bridge,
                runtimeServices,
                incomingEdgeByInputSocket,
                state.outputBySocket};

            uint64_t inputHash = 0;
            bool canHash = kernels.computeInputHash(node, kernelState, inputValues, inputHash);
            bool reusedCache = false;
            if (canHash) {
                const auto hashIt = lastHashByNodeId.find(node.id.value);
                const auto cacheIt = cachedOutputsByNodeId.find(node.id.value);
                if (hashIt != lastHashByNodeId.end() &&
                    cacheIt != cachedOutputsByNodeId.end() &&
                    hashIt->second == inputHash &&
                    cacheIt->second.size() == node.outputs.size()) {
                    outputValues = cacheIt->second;
                    reusedCache = true;
                }
            }

            if (!reusedCache) {
                outputValues.resize(node.outputs.size());
                std::vector<const NodeDataBlock*> inputDataValues(inputValues.size(), nullptr);
                for (std::size_t inputIndex = 0; inputIndex < inputValues.size(); ++inputIndex) {
                    const EvaluatedSocketValue* inputValue = inputValues[inputIndex];
                    if (inputValue && inputValue->status == EvaluatedSocketStatus::Value) {
                        inputDataValues[inputIndex] = &inputValue->data;
                    }
                }
                initializeOutputsFromInputs(node, inputDataValues, outputValues);
                kernels.executeNode(node, kernelState, inputValues, outputValues);

                if (canHash) {
                    lastHashByNodeId[node.id.value] = inputHash;
                    cachedOutputsByNodeId[node.id.value] = outputValues;
                }
            }

            for (std::size_t outputIndex = 0; outputIndex < node.outputs.size(); ++outputIndex) {
                state.outputBySocket[makeSocketKey(node.id, node.outputs[outputIndex].id)] =
                    makeValueSocketValue(outputValues[outputIndex]);
            }
        }
    }

    if (runtimeServices.contactPreviewStore) {
        runtimeServices.contactPreviewStore->endFrame();
    }

    if (outState) {
        outState->sourceSocketByInputSocket = std::move(state.sourceSocketByInputSocket);
        outState->outputBySocket = std::move(state.outputBySocket);
    }
}

void NodeGraphRuntime::clearNodeCaches() {
    lastHashByNodeId.clear();
    cachedOutputsByNodeId.clear();
}

void NodeGraphRuntime::invalidateNodeCaches(const std::unordered_set<uint32_t>& dirtyNodeIds) {
    if (dirtyNodeIds.empty()) {
        return;
    }

    std::unordered_set<uint32_t> invalidatedNodeIds = dirtyNodeIds;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const NodeGraphEdge& edge : graphState.edges) {
            if (!edge.fromNode.isValid() || !edge.toNode.isValid()) {
                continue;
            }
            if (invalidatedNodeIds.find(edge.fromNode.value) == invalidatedNodeIds.end()) {
                continue;
            }
            if (invalidatedNodeIds.insert(edge.toNode.value).second) {
                changed = true;
            }
        }
    }

    for (uint32_t nodeId : invalidatedNodeIds) {
        lastHashByNodeId.erase(nodeId);
        cachedOutputsByNodeId.erase(nodeId);
    }
}
