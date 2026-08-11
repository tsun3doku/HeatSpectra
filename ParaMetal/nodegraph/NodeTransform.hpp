#pragma once

#include "NodeGraphKernels.hpp"
#include "NodeTransformParams.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

class NodeTransform final : public NodeKernel {
public:
    const char* typeId() const override;
    void execute(NodeKernelEval& eval) const override;
    HashValues computeOutputHashes(const NodeKernelHash& hash) const override;

    static glm::mat4 buildLocalTransform(const TransformNodeParams& params);
    static glm::mat4 buildLocalTransform(const NodeGraphNode& node);
    static std::array<float, 16> buildLocalTransformArray(const NodeGraphNode& node);

private:
    static void combineTransformParams(const NodeGraphNode& node, uint64_t& outHash);
};
