#include "NodeHeatSource.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphHash.hpp"
#include "NodeHeatSourceParams.hpp"
#include "NodePayloadRegistry.hpp"

const char* NodeHeatSource::typeId() const {
    return nodegraphtypes::HeatSource;
}

void NodeHeatSource::execute(NodeGraphKernelContext& context) const {
    const HeatSourceNodeParams params = readHeatSourceNodeParams(context.node);
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;
        outputValue.payloadHandle = {};
        if (!payloadRegistry) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        if (!inputMeshValue || inputMeshValue->payloadHandle.key == 0 ||
            valueTypeOf(inputMeshValue->dataType) != NodeGraphValueType::Mesh) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }
        const float temperature = static_cast<float>(params.temperature);
        const uint64_t meshPayloadHash = payloadRegistry->resolvePayloadHash(inputMeshValue->dataType, inputMeshValue->payloadHandle);
        HeatSourceData payload{};
        payload.meshHandle = inputMeshValue->payloadHandle;
        payload.meshPayloadHash = meshPayloadHash;
        payload.temperature = temperature;
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(payload));
        populateMetadata(outputValue, payloadRegistry);
    }
}


bool NodeHeatSource::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const HeatSourceNodeParams params = readHeatSourceNodeParams(context.node);
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, inputMeshValue);
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.temperature));
    return true;
}
