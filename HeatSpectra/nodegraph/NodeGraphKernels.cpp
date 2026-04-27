#include "NodeGraphKernels.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeContact.hpp"
#include "NodeHeatReceiver.hpp"
#include "NodeHeatSolve.hpp"
#include "NodeHeatSource.hpp"
#include "NodeVoronoi.hpp"
#include "NodeGroup.hpp"
#include "NodeModel.hpp"
#include "NodeTransform.hpp"
#include "NodeRemesh.hpp"

#include <utility>

NodeGraphKernels::NodeGraphKernels() {
    registerDefaultKernels();
}

bool NodeGraphKernels::hasKernel(const NodeTypeId& typeId) const {
    return kernelByTypeId.find(getNodeTypeId(typeId)) != kernelByTypeId.end();
}

bool NodeGraphKernels::computeInputHash(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const std::vector<const EvaluatedSocketValue*>& inputs,
    uint64_t& outHash) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return false;
    }

    NodeGraphKernelHashContext context{node, inputs, executionState};
    return kernelIt->second->computeInputHash(context, outHash);
}

void NodeGraphKernels::executeNode(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const std::vector<const EvaluatedSocketValue*>& inputs,
    std::vector<NodeDataBlock>& outputs) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return;
    }

    NodeGraphKernelContext context{node, inputs, outputs, executionState};

    kernelIt->second->execute(context);
}

void NodeGraphKernels::registerDefaultKernels() {
    registerKernel(std::make_unique<NodeModel>());
    registerKernel(std::make_unique<NodeTransform>());
    registerKernel(std::make_unique<NodeGroup>());
    registerKernel(std::make_unique<NodeRemesh>());
    registerKernel(std::make_unique<NodeHeatReceiver>());
    registerKernel(std::make_unique<NodeHeatSource>());
    registerKernel(std::make_unique<NodeContact>());
    registerKernel(std::make_unique<NodeVoronoi>());
    registerKernel(std::make_unique<NodeHeatSolve>());
}

void NodeGraphKernels::registerKernel(std::unique_ptr<NodeKernel> kernel) {
    if (!kernel || !kernel->typeId() || kernel->typeId()[0] == '\0') {
        return;
    }

    const NodeTypeId canonicalTypeId = getNodeTypeId(kernel->typeId());
    kernelByTypeId[canonicalTypeId] = kernel.get();
    kernels.push_back(std::move(kernel));
}

