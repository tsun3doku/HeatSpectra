#include "NodeGraphDisplay.hpp"

#include "NodeGraphDataTypes.hpp"
#include "NodeGraphEvaluation.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphProductTypes.hpp"
#include "NodeGraphUtils.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/package/RuntimePackageManager.hpp"

#include <unordered_set>

std::unordered_set<uint64_t> NodeGraphDisplay::computeDisplayKeys(
    const NodeGraphState& graphState,
    const NodeGraphEvaluation& evaluation,
    const RuntimePackageManager& packages,
    const NodePayloadRegistry* payloadRegistry) const {

    std::unordered_set<uint64_t> selectedKeys;

    for (const auto& [id, node] : graphState.nodes) {
        if (!node.state.isPrimaryDisplay()) {
            continue;
        }

        for (const NodeGraphSocket& output : node.outputs) {
            const uint64_t socketKey = NodeSocketKey(node.id, output.id);
            const EvaluatedSocketValue* value = evaluation.outputFor(socketKey);
            const NodeDataBlock* block = (value && value->status == EvaluatedSocketStatus::Value) ? &value->data : nullptr;

            addDisplayKeys(socketKey, block, packages, payloadRegistry, selectedKeys);
        }
    }

    return selectedKeys;
}

void NodeGraphDisplay::addDisplayKeys(
    uint64_t socketKey,
    const NodeDataBlock* block,
    const RuntimePackageManager& packages,
    const NodePayloadRegistry* payloadRegistry,
    std::unordered_set<uint64_t>& selectedKeys) const {

    if (socketKey == 0) {
        return;
    }

    if (packages.findAny<ModelPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        return;
    }

    if (const RemeshPackage* remeshPkg = packages.findAny<RemeshPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        if (remeshPkg->sourceModelProduct.isValid()) {
            selectedKeys.insert(remeshPkg->sourceModelProduct.outputSocketKey);
        }
        return;
    }

    if (packages.findAny<PointPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        return;
    }

    if (const VoronoiPackage* voronoiPkg = packages.findAny<VoronoiPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        for (const ProductHandle& handle : voronoiPkg->globalRemeshProducts) {
            selectedKeys.insert(handle.outputSocketKey);
        }
        for (const ProductHandle& handle : voronoiPkg->globalModelProducts) {
            selectedKeys.insert(handle.outputSocketKey);
        }
        if (voronoiPkg->pointsPayloadHandle.key != 0) {
            selectedKeys.insert(voronoiPkg->pointsPayloadHandle.key);
        }
        return;
    }

    if (const HeatPackage* heatPkg = packages.findAny<HeatPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        for (const HeatModelPackage& model : heatPkg->models) {
            selectedKeys.insert(model.modelProduct.outputSocketKey);
            selectedKeys.insert(model.remeshProduct.outputSocketKey);
        }
        if (heatPkg->domainVoronoiProduct.isValid()) {
            selectedKeys.insert(heatPkg->domainVoronoiProduct.outputSocketKey);
        }
        return;
    }

    // Fallback for non-package authoring nodes (e.g. previewing a HeatModel node directly)
    if (block && block->dataType == payloadtypes::HeatModel && payloadRegistry) {
        NodeDataHandle currentMeshHandle{};
        if (payloadRegistry->resolveRemesh(block->payloadHandle, &currentMeshHandle) &&
            currentMeshHandle.key != 0) {
            if (const RemeshPackage* remesh = packages.findAny<RemeshPackage>(currentMeshHandle.key)) {
                selectedKeys.insert(currentMeshHandle.key);
                if (remesh->sourceModelProduct.isValid()) {
                    selectedKeys.insert(remesh->sourceModelProduct.outputSocketKey);
                }
            } else {
                selectedKeys.insert(currentMeshHandle.key);
            }
        }
        return;
    }
}
