#pragma once

#include "NodeGraphKernels.hpp"

class NodeGroup final : public NodeKernel {
public:
    const char* typeId() const override;
    void execute(NodeGraphKernelContext& context) const override;
    bool computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const override;

private:
    static bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs);
    static uint32_t resolveTargetGroupId(GeometryData& geometry, const std::string& targetGroupName);
    static bool applyAssignment(
        GeometryData& geometry,
        const std::string& sourceGroupName,
        const std::string& targetGroupName);
};
