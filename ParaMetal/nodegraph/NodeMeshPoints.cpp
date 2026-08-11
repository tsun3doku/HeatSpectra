#include "NodeMeshPoints.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "domain/GeometryData.hpp"
#include "domain/PointData.hpp"

#include <limits>

const char* NodeMeshPoints::typeId() const {
    return nodegraphtypes::MeshPoints;
}

void NodeMeshPoints::execute(NodeKernelEval& eval) const {
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;
    const std::size_t geometrySocketIndex = inputIndexOf(eval.node, "Geometry");
    const NodeDataBlock* inputData =
        (geometrySocketIndex < eval.inputs.size() && !eval.inputs[geometrySocketIndex].empty())
            ? eval.inputs[geometrySocketIndex].front() : nullptr;

    for (std::size_t outputIndex = 0; outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != payloadtypes::Points) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        if (!inputData || inputData->payloadHandle.key == 0) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        const GeometryData* geometryData = payloadRegistry->resolveGeometry(inputData->payloadHandle);
        if (!geometryData || geometryData->pointPositions.empty()) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        PointData payload{};
        const std::size_t vertexCount = geometryData->pointPositions.size() / 3;
        payload.positions.reserve(vertexCount);
        payload.domainMinimum = glm::vec3(std::numeric_limits<float>::max());
        payload.domainMaximum = glm::vec3(std::numeric_limits<float>::lowest());
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const glm::vec3 position(
                geometryData->pointPositions[3 * i + 0],
                geometryData->pointPositions[3 * i + 1],
                geometryData->pointPositions[3 * i + 2]);
            payload.positions.push_back(glm::vec4(position, 1.0f));
            payload.domainMinimum = glm::min(payload.domainMinimum, position);
            payload.domainMaximum = glm::max(payload.domainMaximum, position);
        }

        if (!payloadRegistry->resolveLocalToWorld(
                inputData->payloadHandle,
                payload.localToWorld)) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }
        payload.active = true;
        const uint64_t payloadKey = NodeSocketKey(eval.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, payload, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeMeshPoints::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::MeshPoints);
    HashNodeCache::combineSocket(hashValue, hash, "Geometry", HashDomain::Geometry);

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}
