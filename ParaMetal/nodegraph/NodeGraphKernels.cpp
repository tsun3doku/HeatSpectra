#include "NodeGraphKernels.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeHeatModel.hpp"
#include "NodeHeatSolve.hpp"
#include "NodeMeshPoints.hpp"
#include "NodePoints.hpp"
#include "NodeVoronoi.hpp"
#include "NodeGroup.hpp"
#include "NodeModel.hpp"
#include "NodeTransform.hpp"
#include "NodeRemesh.hpp"
#include "NodeMerge.hpp"
#include "NodeSerialTemperature.hpp"

#include <utility>

NodeGraphKernels::NodeGraphKernels() {
    registerDefaultKernels();
}

bool NodeGraphKernels::hasKernel(const NodeTypeId& typeId) const {
    return kernelByTypeId.find(getNodeTypeId(typeId)) != kernelByTypeId.end();
}

HashValues NodeGraphKernels::computeOutputHashes(
    const NodeGraphNode& node,
    const NodeKernelRuntime& runtime,
    const std::vector<std::vector<const NodeDataBlock*>>& inputs) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return {};
    }

    NodeKernelHash hash{node, inputs, runtime};
    return kernelIt->second->computeOutputHashes(hash);
}

void NodeGraphKernels::executeNode(
    const NodeGraphNode& node,
    const NodeKernelRuntime& runtime,
    const std::vector<std::vector<const NodeDataBlock*>>& inputs,
    std::vector<NodeDataBlock>& outputs,
    HashValues outputHashes) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return;
    }

    NodeKernelEval eval{node, inputs, outputs, runtime, outputHashes};

    kernelIt->second->execute(eval);
}

void NodeGraphKernels::registerDefaultKernels() {
    registerKernel(std::make_unique<NodeModel>());
    registerKernel(std::make_unique<NodeTransform>());
    registerKernel(std::make_unique<NodeGroup>());
    registerKernel(std::make_unique<NodeRemesh>());
    registerKernel(std::make_unique<NodeHeatModel>());
    registerKernel(std::make_unique<NodeVoronoi>());
    registerKernel(std::make_unique<NodeHeatSolve>());
    registerKernel(std::make_unique<NodeMeshPoints>());
    registerKernel(std::make_unique<NodePoints>());
    registerKernel(std::make_unique<NodeMerge>());
    registerKernel(std::make_unique<NodeSerialTemperature>());
}

void NodeGraphKernels::registerKernel(std::unique_ptr<NodeKernel> kernel) {
    if (!kernel || !kernel->typeId() || kernel->typeId()[0] == '\0') {
        return;
    }

    const NodeTypeId canonicalTypeId = getNodeTypeId(kernel->typeId());
    kernelByTypeId[canonicalTypeId] = kernel.get();
    kernels.push_back(std::move(kernel));
}

