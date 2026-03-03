#pragma once

#include "NodeGraphKernels.hpp"

class NodeRemesh final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;

private:
    static bool getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue = false);
    static int getIntParamValue(const NodeGraphNode& node, uint32_t parameterId, int defaultValue);
    static double getFloatParamValue(const NodeGraphNode& node, uint32_t parameterId, double defaultValue);
    static bool setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value);
    static bool tryGetRemeshedModelGeometry(
        const NodeRuntimeServices& services,
        uint32_t targetRuntimeModelId,
        std::vector<float>& outPointPositions,
        std::vector<uint32_t>& outTriangleIndices);
    static bool tryBuildRemeshedGeometry(
        const NodeRuntimeServices& services,
        const NodeDataBlock* upstreamGeometryValue,
        GeometryData& outGeometry);
    static bool tryBuildRemeshedGeometryForModel(
        const NodeRuntimeServices& services,
        uint32_t targetGraphModelId,
        const NodeDataBlock* upstreamGeometryValue,
        GeometryData& outGeometry);
};
