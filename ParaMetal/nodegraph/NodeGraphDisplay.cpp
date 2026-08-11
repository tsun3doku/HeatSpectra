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
        if (remeshPkg->sourceMeshHandle.key != 0) {
            selectedKeys.insert(remeshPkg->sourceMeshHandle.key);
        }
        return;
    }

    if (block && block->dataType == payloadtypes::HeatModel && payloadRegistry) {
        NodeDataHandle currentMeshHandle{};
        payloadRegistry->resolveRemesh(block->payloadHandle, &currentMeshHandle);
        const RemeshPackage* remeshPackage =
            packages.findAny<RemeshPackage>(currentMeshHandle.key);
        const NodeDataHandle sourceModelHandle = remeshPackage
            ? remeshPackage->sourceMeshHandle
            : currentMeshHandle;
        if (sourceModelHandle.key != 0) {
            selectedKeys.insert(sourceModelHandle.key);
        }
        if (currentMeshHandle.key != 0 &&
            currentMeshHandle.key != sourceModelHandle.key &&
            packages.findAny<RemeshPackage>(currentMeshHandle.key)) {
            selectedKeys.insert(currentMeshHandle.key);
        }
        return;
    }

    if (packages.findAny<PointPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        return;
    }

    if (const VoronoiPackage* voronoiPkg = packages.findAny<VoronoiPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        if (voronoiPkg->modelMeshHandle.key != 0) {
            selectedKeys.insert(voronoiPkg->modelMeshHandle.key);
        }
        if (voronoiPkg->modelRemeshHandle.key != 0) {
            selectedKeys.insert(voronoiPkg->modelRemeshHandle.key);
        }
        if (voronoiPkg->pointsPayloadHandle.key != 0) {
            selectedKeys.insert(voronoiPkg->pointsPayloadHandle.key);
        }
        return;
    }

    if (const ContactPackage* contactPkg = packages.findAny<ContactPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        selectedKeys.insert(contactPkg->modelARemeshProduct.outputSocketKey);
        selectedKeys.insert(contactPkg->modelBRemeshProduct.outputSocketKey);
        return;
    }

    if (const HeatPackage* heatPkg = packages.findAny<HeatPackage>(socketKey)) {
        selectedKeys.insert(socketKey);
        for (const HeatModelPackage& model : heatPkg->models) {
            selectedKeys.insert(model.modelProduct.outputSocketKey);
            selectedKeys.insert(model.remeshProduct.outputSocketKey);
            selectedKeys.insert(model.voronoiProduct.outputSocketKey);
        }
        for (const ProductHandle& handle : heatPkg->contactProducts) {
            selectedKeys.insert(handle.outputSocketKey);
        }
        return;
    }

}
