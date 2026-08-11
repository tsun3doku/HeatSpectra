#include "NodePoints.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodePointsParams.hpp"
#include "domain/PointData.hpp"

#include <cmath>

const char* NodePoints::typeId() const {
    return nodegraphtypes::Points;
}

void NodePoints::execute(NodeKernelEval& eval) const {
    const PointsNodeParams params = readPointsNodeParams(eval.node);
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputValue.dataType != payloadtypes::Points || params.pointCount == 0) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        PointData payload{};
        payload.positions.reserve(params.pointCount);

        // Uniform grid generation within [-dim/2, dim/2] per axis
        const uint32_t count = params.pointCount;
        const float dimX = std::max(params.dimX, 0.001f);
        const float dimY = std::max(params.dimY, 0.001f);
        const float dimZ = std::max(params.dimZ, 0.001f);

        const float volume = dimX * dimY * dimZ;
        const float targetSpacing = std::cbrt(volume / static_cast<float>(count));

        const uint32_t nx = std::max(1u, static_cast<uint32_t>(std::round(dimX / targetSpacing)));
        const uint32_t ny = std::max(1u, static_cast<uint32_t>(std::round(dimY / targetSpacing)));
        const uint32_t nz = std::max(1u, static_cast<uint32_t>(std::round(dimZ / targetSpacing)));

        const float stepX = dimX / static_cast<float>(nx);
        const float stepY = dimY / static_cast<float>(ny);
        const float stepZ = dimZ / static_cast<float>(nz);

        payload.domainMinimum = glm::vec3(-dimX, -dimY, -dimZ) * 0.5f;
        payload.domainMaximum = glm::vec3( dimX,  dimY,  dimZ) * 0.5f;

        uint32_t generated = 0;
        for (uint32_t ix = 0; ix < nx && generated < count; ++ix) {
            for (uint32_t iy = 0; iy < ny && generated < count; ++iy) {
                for (uint32_t iz = 0; iz < nz && generated < count; ++iz) {
                    glm::vec4 pos{};
                    pos.x = -dimX * 0.5f + stepX * (static_cast<float>(ix) + 0.5f);
                    pos.y = -dimY * 0.5f + stepY * (static_cast<float>(iy) + 0.5f);
                    pos.z = -dimZ * 0.5f + stepZ * (static_cast<float>(iz) + 0.5f);
                    pos.w = 1.0f;
                    payload.positions.push_back(pos);
                    ++generated;
                }
            }
        }

        payload.active = true;
        const uint64_t payloadKey = NodeSocketKey(eval.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, payload, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodePoints::computeOutputHashes(const NodeKernelHash& hash) const {
    const PointsNodeParams params = readPointsNodeParams(hash.node);
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Points);
    HashBuilder::combine(hashValue, static_cast<uint64_t>(params.pointCount));
    HashBuilder::combineFloat(hashValue, params.dimX);
    HashBuilder::combineFloat(hashValue, params.dimY);
    HashBuilder::combineFloat(hashValue, params.dimZ);

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}
