#pragma once

#include "NodeGraphKernels.hpp"

class NodeContactPair final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;

private:
    static uint64_t makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
    static const NodeDataBlock* resolveInputValueForSocket(
        const NodeGraphNode& node,
        NodeGraphSocketId inputSocketId,
        const NodeGraphKernelExecutionState& executionState);
    static double getFloatParamValue(const NodeGraphNode& node, uint32_t parameterId, double defaultValue);
    static bool getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue);
    static bool setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value);
};
