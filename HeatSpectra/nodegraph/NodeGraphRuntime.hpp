#pragma once

#include "NodeGraphKernels.hpp"

#include <cstdint>
#include <unordered_map>

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
    static uint64_t makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphKernels kernels;
    NodeGraphState graphState{};
    std::unordered_map<uint32_t, const NodeGraphNode*> nodeById{};
};
