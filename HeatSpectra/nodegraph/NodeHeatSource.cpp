#include "NodeHeatSource.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
#include "NodePayloadRegistry.hpp"

const char* NodeHeatSource::typeId() const {
    return nodegraphtypes::HeatSource;
}

bool NodeHeatSource::execute(NodeGraphKernelContext& context) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodePayloadType::None;
        outputValue.payloadHandle = {};
        if (!payloadRegistry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        if (!inputMeshValue || inputMeshValue->payloadHandle.key == 0) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        outputValue.dataType = NodePayloadType::HeatSource;
        const float temperature = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::heatsource::Temperature, 100.0));
        const uint64_t meshPayloadHash = payloadHashForDataBlock(*inputMeshValue, payloadRegistry);
        HeatSourceData payload{};
        payload.meshHandle = inputMeshValue->payloadHandle;
        payload.temperature = temperature;
        payload.payloadHash = NodeGraphHash::start();
        NodeGraphHash::combine(payload.payloadHash, meshPayloadHash);
        NodeGraphHash::combineFloat(payload.payloadHash, payload.temperature);
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(payload));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}


bool NodeHeatSource::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    if (!inputMeshValue) {
        NodeGraphHash::combine(outHash, 0u);
        return true;
    }

    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputMeshValue->dataType));
    NodeGraphHash::combine(outHash, inputMeshValue->payloadHandle.key);
    NodeGraphHash::combine(outHash, inputMeshValue->payloadHandle.revision);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputMeshValue->payloadHandle.count));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::heatsource::Temperature, 100.0)));
    return true;
}
