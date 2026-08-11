#include "NodeContact.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraph.hpp"
#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeContactParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

#include <cstdint>

const char* NodeContact::typeId() const {
    return nodegraphtypes::Contact;
}

void NodeContact::execute(NodeKernelEval& eval) const {
    const std::size_t emitterIndex = inputIndexOf(eval.node, "SurfaceA");
    const std::size_t receiverIndex = inputIndexOf(eval.node, "SurfaceB");
    const NodeDataBlock* emitterInput =
        (emitterIndex < eval.inputs.size() && !eval.inputs[emitterIndex].empty())
            ? eval.inputs[emitterIndex].front() : nullptr;
    const NodeDataBlock* receiverInput =
        (receiverIndex < eval.inputs.size() && !eval.inputs[receiverIndex].empty())
            ? eval.inputs[receiverIndex].front() : nullptr;

    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;

    NodeDataHandle emitterMeshHandle{};
    bool hasEmitterEndpoint = false;
    if (payloadRegistry && emitterInput && emitterInput->payloadHandle.key != 0) {
        hasEmitterEndpoint = payloadRegistry->resolveRemesh(
            emitterInput->payloadHandle, &emitterMeshHandle) != nullptr;
    }

    NodeDataHandle receiverMeshHandle{};
    bool hasReceiverEndpoint = false;
    if (payloadRegistry && receiverInput && receiverInput->payloadHandle.key != 0) {
        hasReceiverEndpoint = payloadRegistry->resolveRemesh(
            receiverInput->payloadHandle, &receiverMeshHandle) != nullptr;
    }

    const bool hasValidContact = hasEmitterEndpoint && hasReceiverEndpoint &&
        !(emitterMeshHandle == receiverMeshHandle);

    const ContactNodeParams params = readContactNodeParams(eval.node);

    for (std::size_t outputIndex = 0;
         outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size();
         ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != payloadtypes::Contact || !hasValidContact) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        ContactData contactData{};
        contactData.pair.endpointA.payloadHandle = emitterInput->payloadHandle;
        contactData.pair.endpointA.meshHandle = emitterMeshHandle;
        contactData.pair.endpointB.payloadHandle = receiverInput->payloadHandle;
        contactData.pair.endpointB.meshHandle = receiverMeshHandle;
        contactData.pair.minNormalDot = static_cast<float>(params.minNormalDot);
        contactData.pair.contactRadius = static_cast<float>(params.contactRadius);
        contactData.pair.hasValidContact = true;
        contactData.active = true;

        const uint64_t payloadKey = NodeSocketKey(eval.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, contactData, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeContact::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Contact);
    HashNodeCache::combineSocket(hashValue, hash, "SurfaceA", HashDomain::Geometry);
    HashNodeCache::combineSocket(hashValue, hash, "SurfaceB", HashDomain::Geometry);

    const ContactNodeParams params = readContactNodeParams(hash.node);
    HashBuilder::combineFloat(hashValue, static_cast<float>(params.minNormalDot));
    HashBuilder::combineFloat(hashValue, static_cast<float>(params.contactRadius));

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}
