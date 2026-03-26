#pragma once

#include "NodeGraphCompiler.hpp"
#include "NodeGraphRuntime.hpp"

#include <cstdint>
#include <unordered_map>

class NodeGraphBridge;

class NodeGraphController {
public:
    NodeGraphController(
        NodeGraphBridge* bridge = nullptr,
        const NodeRuntimeServices& services = {});

    void applyPendingChanges();
    void tick();
    bool canExecuteHeatSolve() const;
    const NodeGraphCompiled& compiledState() const;

private:
    void projectGeometryOutputs(const NodeGraphRuntimeExecutionState& execState);
    void projectRemeshOutputs(const NodeGraphRuntimeExecutionState& execState);
    void projectContactOutputs(const NodeGraphRuntimeExecutionState& execState);
    void projectSystemOutputs(const NodeGraphRuntimeExecutionState& execState);
    void pruneProjectedGeometryOutputs();
    void pruneProjectedRemeshRevisions();
    static uint64_t socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);

    NodeGraphBridge* bridge = nullptr;
    NodeRuntimeServices runtimeServices{};
    NodeGraphRuntime runtime;
    uint64_t revisionSeen = 0;
    NodeGraphCompiled plan{};
    std::unordered_map<uint32_t, uint64_t> projectedRemeshRevisionByNodeId{};
    std::unordered_map<uint32_t, NodeDataHandle> projectedRemeshIntrinsicHandleByNodeId{};
    std::unordered_map<uint64_t, uint64_t> projectedPayloadRevisionBySocketKey{};
    std::unordered_map<uint64_t, NodeDataType> projectedPayloadTypeBySocketKey{};
    std::unordered_map<uint64_t, uint32_t> projectedGeometryNodeModelIdBySocketKey{};
};
