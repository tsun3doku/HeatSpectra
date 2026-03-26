#include "NodeHeatSource.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
#include "NodePayloadRegistry.hpp"

const char* NodeHeatSource::typeId() const {
    return nodegraphtypes::HeatSource;
}

bool NodeHeatSource::execute(NodeGraphKernelContext& context) const {
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

        if (inputGeometryValue->payloadHandle.key == 0) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        outputValue.dataType = NodeDataType::HeatSource;
        const float temperature = static_cast<float>(NodePanelUtils::readFloatParam(
            context.node, nodegraphparams::heatsource::Temperature, 100.0));
        HeatSourceData payload{};
        payload.geometryHandle = inputGeometryValue->payloadHandle;
        payload.temperature = temperature;
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(payload));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}


bool NodeHeatSource::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
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
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::heatsource::Temperature, 100.0)));
    return true;
}
