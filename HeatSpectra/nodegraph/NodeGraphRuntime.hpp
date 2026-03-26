#pragma once

#include "NodeGraphKernels.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class NodeGraphBridge;

struct NodeGraphRuntimeExecutionState {
    std::unordered_map<uint64_t, uint64_t> sourceSocketByInputSocket;
    std::unordered_map<uint64_t, NodeDataBlock> outputValueBySocket;
};

class NodeGraphRuntime {
public:
    NodeGraphRuntime(NodeGraphBridge* nodeGraphBridge = nullptr, const NodeRuntimeServices& services = {});
    ~NodeGraphRuntime();

    void applyDelta(const NodeGraphDelta& delta);
    void tick(NodeGraphRuntimeExecutionState* outState = nullptr);

    const NodeGraphState& state() const {
        return graphState;
    }
private:
    void applyChange(const NodeGraphChange& change);
    void executeDataflow(NodeGraphRuntimeExecutionState* outState);
    void rebuildNodeById();
    void clearNodeCaches();
    void invalidateNodeCaches(const std::unordered_set<uint32_t>& dirtyNodeIds);

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphKernels kernels;
    NodeGraphState graphState{};
    std::unordered_map<uint32_t, const NodeGraphNode*> nodeById{};
    std::unordered_map<uint32_t, uint64_t> lastHashByNodeId{};
    std::unordered_map<uint32_t, std::vector<NodeDataBlock>> cachedOutputsByNodeId{};
};
