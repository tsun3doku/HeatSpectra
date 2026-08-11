#include "NodeGraphRuntime.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

NodeGraphRuntime::NodeGraphRuntime(NodePayloadRegistry* registry)
    : payloadRegistry(registry) {
}

NodeGraphRuntime::~NodeGraphRuntime() = default;

void NodeGraphRuntime::publishOutputs(
    const NodeGraphNode& node,
    const std::vector<NodeDataBlock>& outputs,
    NodeGraphEvaluation& state,
    bool frozen) const {
    const std::size_t count = std::min(outputs.size(), node.outputs.size());
    for (std::size_t i = 0; i < count; ++i) {
        EvaluatedSocketValue value{};
        value.status = EvaluatedSocketStatus::Value;
        value.data = outputs[i];
        value.data.isFrozen = frozen;
        state.outputsBySocket[NodeSocketKey(node.id, node.outputs[i].id).value] = value;
    }
}

bool NodeGraphRuntime::publishCachedOutputs(
    const NodeGraphNode& node,
    NodeGraphEvaluation& state) const {
    const auto it = cachedOutputsByNodeId.find(node.id.value);
    if (it == cachedOutputsByNodeId.end() ||
        it->second.outputs.size() != node.outputs.size()) {
        return false;
    }

    publishOutputs(node, it->second.outputs, state, it->second.pinned);
    return true;
}

void NodeGraphRuntime::publishBlockedOutputs(
    const NodeGraphNode& node,
    EvaluatedSocketStatus status,
    const std::string& error,
    NodeGraphEvaluation& state) const {
    EvaluatedSocketValue blockedValue{};
    blockedValue.status = status;
    if (status == EvaluatedSocketStatus::Error) {
        blockedValue.error = error;
    }
    for (const NodeGraphSocket& outputSocket : node.outputs) {
        state.outputsBySocket[NodeSocketKey(node.id, outputSocket.id).value] = blockedValue;
    }
}

NodeGraphRuntime::EvaluatedNodeInputs NodeGraphRuntime::evaluateNodeInputs(
    const NodeGraphNode& node,
    const NodeGraphState& graphState,
    const NodeGraphEvaluation& state) const {

    EvaluatedNodeInputs result;
    result.values.assign(node.inputs.size(), {});

    for (std::size_t inputIndex = 0; inputIndex < node.inputs.size(); ++inputIndex) {
        const NodeGraphSocket& inputSocket = node.inputs[inputIndex];
        const std::vector<const NodeGraphEdge*> incomingEdges =
            graphState.edges.incoming(node.id, inputSocket.id);

        if (incomingEdges.empty()) {
            if (!inputSocket.required) {
                continue;
            }
            result.status = EvaluatedSocketStatus::Missing;
            return result;
        }

        for (const NodeGraphEdge* edgePtr : incomingEdges) {
            if (!edgePtr) {
                continue;
            }
            const NodeGraphEdge& edge = *edgePtr;
            const auto outputIt = state.outputsBySocket.find(NodeSocketKey(edge.fromNode, edge.fromSocket).value);
            if (outputIt == state.outputsBySocket.end()) {
                if (!inputSocket.required) {
                    continue;
                }
                result.status = EvaluatedSocketStatus::Missing;
                return result;
            }

            const EvaluatedSocketValue& inputValue = outputIt->second;
            if (inputValue.status != EvaluatedSocketStatus::Value) {
                result.status = inputValue.status;
                result.error = inputValue.error;
                return result;
            }

            result.values[inputIndex].push_back(&inputValue.data);
        }
    }

    return result;
}

void NodeGraphRuntime::applyDelta(const NodeGraphDelta& delta) {
    if (!delta.changes.empty()) {
        bool topologyChanged = false;
        for (const NodeGraphChange& change : delta.changes) {
            if (change.reason == NodeGraphChangeReason::Topology) {
                topologyChanged = true;
            }
            applyChange(change);
        }
        if (topologyChanged) {
            for (auto it = cachedOutputsByNodeId.begin(); it != cachedOutputsByNodeId.end();) {
                if (it->second.pinned) {
                    ++it;
                } else {
                    it = cachedOutputsByNodeId.erase(it);
                }
            }
            if (payloadRegistry && cachedOutputsByNodeId.empty()) {
                payloadRegistry->clear();
            }
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
        graphState.nodes[change.node.id.value] = change.node;
        break;
    }
    case NodeGraphChangeType::NodeRemoved:
        graphState.nodes.erase(change.nodeId.value);
        {
            std::vector<NodeGraphEdgeId> removedEdgeIds;
            for (const auto& [edgeKey, edge] : graphState.edges) {
                if (edge.fromNode == change.nodeId || edge.toNode == change.nodeId) {
                    removedEdgeIds.push_back(edge.id);
                }
            }
            for (NodeGraphEdgeId edgeId : removedEdgeIds) {
                graphState.edges.remove(edgeId);
            }
        }
        break;
    case NodeGraphChangeType::EdgeUpsert:
        graphState.edges.upsert(change.edge);
        break;
    case NodeGraphChangeType::EdgeRemoved:
        graphState.edges.remove(change.edgeId);
        break;
    }
}

void NodeGraphRuntime::execute(const NodeGraphCompiled& compiled) {
    if (graphState.nodes.size() > 0 && compiled.executionOrder.size() != graphState.nodes.size()) {
        currentEvaluation.outputsBySocket.clear();
        return;
    }

    const std::vector<NodeGraphNodeId>& executionOrder = compiled.executionOrder;
    currentEvaluation.outputsBySocket.clear();
    currentEvaluation.outputsBySocket.reserve(graphState.edges.size() * 2);
    NodeGraphEvaluation& state = currentEvaluation;

    for (NodeGraphNodeId nodeId : executionOrder) {
        auto it = graphState.nodes.find(nodeId.value);
        if (it == graphState.nodes.end()) {
            continue;
        }

        const NodeGraphNode& node = it->second;

        bool pinned = (node.state.frozenState() == NodeGraphNodeState::FrozenState::Frozen);
        if (!pinned) {
            for (NodeGraphNodeId upstreamNodeId : graphState.edges.upstreamNodes(node.id)) {
                auto cacheIt = cachedOutputsByNodeId.find(upstreamNodeId.value);
                if (cacheIt != cachedOutputsByNodeId.end() && cacheIt->second.pinned) {
                    pinned = true;
                    break;
                }
            }
        }

        // Pinned nodes bypass input evaluation and republish their cached output
        if (pinned) {
            auto cacheIt = cachedOutputsByNodeId.find(node.id.value);
            if (cacheIt != cachedOutputsByNodeId.end()) {
                cacheIt->second.pinned = true;
            }
            if (!publishCachedOutputs(node, state)) {
                publishBlockedOutputs(
                    node,
                    EvaluatedSocketStatus::Error,
                    "Frozen node has no cached output.",
                    state);
            }
            continue;
        }

        // Evaluate inputs
        EvaluatedNodeInputs inputs = evaluateNodeInputs(node, graphState, state);
        if (!inputs.ready()) {
            publishBlockedOutputs(node, inputs.status, inputs.error, state);
            continue;
        }

        // Live evaluation
        const NodeTypeId typeId = getNodeTypeId(node.typeId);
        if (!kernels.hasKernel(typeId)) {
            continue;
        }

        evaluateLiveNode(node, inputs, state);
    }
}

void NodeGraphRuntime::evaluateLiveNode(
    const NodeGraphNode& node,
    const EvaluatedNodeInputs& inputs,
    NodeGraphEvaluation& state) {

    const NodeKernelRuntime kernelRuntime{
        graphState,
        payloadRegistry};

    HashValues outputHashes = kernels.computeOutputHashes(node, kernelRuntime, inputs.values);
    uint64_t cacheHash = outputHashes.full;

    // Cache hit
    const auto cacheIt = cachedOutputsByNodeId.find(node.id.value);
    if (cacheIt != cachedOutputsByNodeId.end() &&
        cacheIt->second.hash == cacheHash &&
        cacheIt->second.outputs.size() == node.outputs.size()) {
        cacheIt->second.pinned = false;
        publishOutputs(node, cacheIt->second.outputs, state, /*frozen=*/false);
        return;
    }

    std::vector<NodeDataBlock> outputValues(node.outputs.size());

    {
        std::unordered_map<std::string, std::string> mergedMetadata;
        std::vector<NodeGraphNodeId> mergedLineage;
        std::unordered_set<uint32_t> seenNodeIds;
        for (const std::vector<const NodeDataBlock*>& socketInputs : inputs.values) {
            for (const NodeDataBlock* input : socketInputs) {
                if (!input) continue;
                for (const auto& metadataEntry : input->metadata) {
                    mergedMetadata[metadataEntry.first] = metadataEntry.second;
                }
                for (NodeGraphNodeId lineageNodeId : input->lineageNodeIds) {
                    if (!lineageNodeId.isValid()) continue;
                    if (seenNodeIds.insert(lineageNodeId.value).second) {
                        mergedLineage.push_back(lineageNodeId);
                    }
                }
            }
        }
        if (node.id.isValid() && seenNodeIds.insert(node.id.value).second) {
            mergedLineage.push_back(node.id);
        }
        mergedMetadata["graph.producer_node_id"] = std::to_string(node.id.value);
        mergedMetadata["graph.producer_type_id"] = getNodeTypeId(node.typeId);
        mergedMetadata["graph.lineage_depth"] = std::to_string(mergedLineage.size());
        for (NodeDataBlock& output : outputValues) {
            output = {};
            output.metadata = mergedMetadata;
            output.lineageNodeIds = mergedLineage;
            populateMetadata(output, nullptr, nullptr);
        }
    }

    kernels.executeNode(node, kernelRuntime, inputs.values, outputValues, outputHashes);

    for (NodeDataBlock& output : outputValues) {
        output.hashes = outputHashes;
    }

    // Store cache and publish
    CachedNodeOutputs& entry = cachedOutputsByNodeId[node.id.value];
    entry.hash = cacheHash;
    entry.outputs = std::move(outputValues);
    entry.pinned = false;
    publishOutputs(node, entry.outputs, state, /*frozen=*/false);
}
