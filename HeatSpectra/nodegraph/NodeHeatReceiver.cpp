#include "NodeHeatReceiver.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphHash.hpp"
#include "NodePayloadRegistry.hpp"

const char* NodeHeatReceiver::typeId() const {
    return nodegraphtypes::HeatReceiver;
}

bool NodeHeatReceiver::execute(NodeGraphKernelContext& context) const {
    const NodeDataBlock* inputGeometryValue = nullptr;
    const NodeGraphSocket* modelSocket = findInputSocket(context.node, "Model");
    if (modelSocket) {
        inputGeometryValue = readInput(context.node, modelSocket->id, context.executionState);
    }
    if (inputGeometryValue && inputGeometryValue->dataType != NodeDataType::Geometry) {
        inputGeometryValue = nullptr;
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodeDataType::None;
        outputValue.payloadHandle = {};
        NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
        if (!inputGeometryValue || !payloadRegistry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        const GeometryData* geometry = payloadRegistry->get<GeometryData>(inputGeometryValue->payloadHandle);
        if (!geometry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        outputValue.dataType = NodeDataType::HeatReceiver;
        HeatReceiverData payload{};
        payload.geometryHandle = inputGeometryValue->payloadHandle;
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(payload));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}

bool NodeHeatReceiver::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeDataBlock* inputGeometryValue = nullptr;
    const NodeGraphSocket* modelSocket = findInputSocket(context.node, "Model");
    if (modelSocket) {
        inputGeometryValue = readInput(context.node, modelSocket->id, context.executionState);
    }
    if (inputGeometryValue && inputGeometryValue->dataType != NodeDataType::Geometry) {
        inputGeometryValue = nullptr;
    }

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));

    if (!inputGeometryValue) {
        NodeGraphHash::combine(outHash, 0u);
        return true;
    }

    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputGeometryValue->dataType));
    NodeGraphHash::combine(outHash, inputGeometryValue->payloadHandle.key);
    NodeGraphHash::combine(outHash, inputGeometryValue->payloadHandle.revision);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputGeometryValue->payloadHandle.count));
    return true;
}

