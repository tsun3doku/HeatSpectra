#include "NodeVoronoi.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeGraphUtils.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeVoronoiParams.hpp"
#include "domain/PointData.hpp"

#include <unordered_set>

const char* NodeVoronoi::typeId() const {
    return nodegraphtypes::Voronoi;
}

void NodeVoronoi::execute(NodeKernelEval& eval) const {
    NodeDataHandle modelMeshHandle;
    NodeDataHandle pointsPayloadHandle;
    DomainType domainType = DomainType::Points;
    bool active = false;
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;

    const std::size_t pointsSocketIndex = inputIndexOf(eval.node, NodeGraphValueType::Points);
    const NodeDataBlock* pointsData =
        (pointsSocketIndex < eval.inputs.size() && !eval.inputs[pointsSocketIndex].empty())
            ? eval.inputs[pointsSocketIndex].front() : nullptr;
    if (pointsData && pointsData->dataType == payloadtypes::Points && pointsData->payloadHandle.key != 0) {
        const PointData* pointData = payloadRegistry ? payloadRegistry->resolvePoints(pointsData->payloadHandle) : nullptr;
        if (pointData && !pointData->positions.empty()) {
            pointsPayloadHandle = pointsData->payloadHandle;
            active = true;
        }
    }

    if (active) {
        const std::size_t meshSocketIndex = inputIndexOf(eval.node, NodeGraphValueType::Remesh);
        const NodeDataBlock* meshData =
            (meshSocketIndex < eval.inputs.size() && !eval.inputs[meshSocketIndex].empty())
                ? eval.inputs[meshSocketIndex].front() : nullptr;
        if (meshData && meshData->payloadHandle.key != 0) {
            NodeDataHandle meshHandle{};
            if (payloadRegistry && payloadRegistry->resolveRemesh(
                    meshData->payloadHandle, &meshHandle)) {
                modelMeshHandle = meshHandle;
                domainType = DomainType::Mesh;
            }
        }
    }

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(eval.node);

    for (std::size_t outputIndex = 0;
         outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size();
         ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != payloadtypes::Voronoi) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        VoronoiData voronoiData{};
        voronoiData.cellSize = static_cast<float>(nodeParams.sdfSize);
        voronoiData.voxelResolution = nodeParams.voxelResolution;
        voronoiData.domainType = domainType;
        voronoiData.modelMeshHandle = modelMeshHandle;
        voronoiData.pointsPayloadHandle = pointsPayloadHandle;
        voronoiData.active = active;
        const uint64_t payloadKey = NodeSocketKey(eval.node.id, eval.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, voronoiData, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeVoronoi::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Voronoi);
    HashNodeCache::combineSocket(hashValue, hash, NodeGraphValueType::Points, HashDomain::Geometry);
    HashNodeCache::combineOptionalSocket(hashValue, hash, NodeGraphValueType::Remesh, HashDomain::Geometry);

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(hash.node);
    HashBuilder::combineFloat(hashValue, static_cast<float>(nodeParams.sdfSize));
    HashBuilder::combine(hashValue, static_cast<uint64_t>(nodeParams.voxelResolution));

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}
