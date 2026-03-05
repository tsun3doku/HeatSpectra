#pragma once

#include "NodeGraphKernels.hpp"

class NodeGroup final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;

private:
    static bool getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue);
    static std::string getStringParamValue(const NodeGraphNode& node, uint32_t parameterId);
    static bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs);
    static uint32_t resolveTargetGroupId(GeometryData& geometry, const std::string& targetGroupName);
    static void applyAssignment(
        GeometryData& geometry,
        const std::string& sourceGroupName,
        const std::string& targetGroupName);
};
