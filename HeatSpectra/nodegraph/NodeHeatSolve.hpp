#pragma once

#include "NodeGraphKernels.hpp"
#include "NodeHeatMaterialPresets.hpp"

#include <vector>

class NodeHeatSolve final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;
    bool computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const override;

private:
    static void populateOutputPayloads(
        NodeGraphKernelContext& context,
        const std::vector<NodeDataHandle>& sourceHandles,
        const std::vector<NodeDataHandle>& receiverGeometryHandles,
        const std::vector<HeatMaterialBindingEntry>& materialBindings,
        bool active,
        bool paused,
        bool resetRequested);
    static NodeGraphNodeId selectHeatSolveNode(
        const NodeGraphState& state,
        const NodeGraphKernelExecutionState& executionState);
    static std::vector<HeatMaterialBindingEntry> parseMaterialBindings(const std::string& serializedBindings);
};
