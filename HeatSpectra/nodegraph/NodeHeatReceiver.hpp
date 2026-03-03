#pragma once

#include "NodeGraphKernels.hpp"

class NodeHeatReceiver final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;
};
