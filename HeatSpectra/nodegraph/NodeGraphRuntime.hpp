#pragma once

#include "NodeGraphDataTypes.hpp"
#include "NodeGraphKernels.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class NodeGraphBridge;

class NodeGraphRuntime {
public:
    NodeGraphRuntime(NodeGraphBridge* nodeGraphBridge = nullptr, const NodeRuntimeServices& services = {});
    ~NodeGraphRuntime();

    void applyDelta(const NodeGraphDelta& delta);
    void tick(NodeGraphEvaluationState* outState, const NodeGraphCompiled& compiled);

    const NodeGraphState& state() const {
        return graphState;
    }
private:
    void applyChange(const NodeGraphChange& change);
    void executeDataflow(NodeGraphEvaluationState* outState, const NodeGraphCompiled& compiled);
    void rebuildNodeById();
    void clearNodeCaches();
    void invalidateNodeCaches(const std::unordered_set<uint32_t>& dirtyNodeIds);
    EvaluatedSocketValue makeMissingSocketValue() const;
    EvaluatedSocketValue makeErrorSocketValue(std::string error) const;
    EvaluatedSocketValue makeValueSocketValue(const NodeDataBlock& data) const;
    void propagateSkippedNodeOutputs(
        const NodeGraphNode& node,
        EvaluatedSocketStatus status,
        const std::string& error,
        NodeGraphEvaluationState& state) const;
    bool evaluateNodeInputs(
        const NodeGraphNode& node,
        const std::unordered_map<uint64_t, const NodeGraphEdge*>& incomingEdgeByInputSocket,
        const NodeGraphEvaluationState& state,
        std::vector<const EvaluatedSocketValue*>& outInputs,
        EvaluatedSocketStatus& outStatus,
        std::string& outError) const;

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphKernels kernels;
    NodeGraphState graphState{};
    std::unordered_map<uint32_t, const NodeGraphNode*> nodeById{};
    std::unordered_map<uint32_t, uint64_t> lastHashByNodeId{};
    std::unordered_map<uint32_t, std::vector<NodeDataBlock>> cachedOutputsByNodeId{};
};
