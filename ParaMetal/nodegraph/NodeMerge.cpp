#include "NodeMerge.hpp"
#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "domain/GeometryData.hpp"
#include "domain/PointData.hpp"

#include <limits>
#include "../util/GeometryUtils.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <vector>
#include <algorithm>

const char* NodeMerge::typeId() const {
    return nodegraphtypes::Merge;
}

void NodeMerge::execute(NodeKernelEval& eval) const {
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;
    if (!payloadRegistry) {
        return;
    }

    const std::size_t geometrySocketIndex = inputIndexOf(eval.node, "Geometry");
    if (geometrySocketIndex >= eval.inputs.size()) {
        return;
    }

    const std::vector<const NodeDataBlock*>& inputs = eval.inputs[geometrySocketIndex];

    // Determine the effective dataType based on the first valid input
    uint8_t dataType = payloadtypes::None;
    for (const NodeDataBlock* block : inputs) {
        if (block && block->payloadHandle.key != 0) {
            if (block->dataType == payloadtypes::Points ||
                block->dataType == payloadtypes::Geometry ||
                block->dataType == payloadtypes::Remesh) {
                dataType = block->dataType;
                break;
            }
        }
    }

    NodeDataBlock& outputValue = eval.outputs[0];
    outputValue = {};

    if (dataType == payloadtypes::Points) {
        std::vector<const PointData*> pointInputs;
        std::vector<std::array<float, 16>> pointMatrices;
        for (const NodeDataBlock* block : inputs) {
            if (block && block->dataType == payloadtypes::Points && block->payloadHandle.key != 0) {
                const PointData* pointData = payloadRegistry->resolvePoints(block->payloadHandle);
                std::array<float, 16> localToWorld{};
                if (pointData && pointData->active && !pointData->positions.empty() &&
                    payloadRegistry->resolveLocalToWorld(block->payloadHandle, localToWorld)) {
                    pointInputs.push_back(pointData);
                    pointMatrices.push_back(localToWorld);
                }
            }
        }

        if (pointInputs.empty()) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            return;
        }

        PointData payload{};
        payload.localToWorld = pointMatrices[0];
        payload.active = true;
        payload.domainMinimum = glm::vec3(std::numeric_limits<float>::max());
        payload.domainMaximum = glm::vec3(std::numeric_limits<float>::lowest());

        glm::mat4 referenceL2W = toMat4(payload.localToWorld);
        glm::mat4 invReferenceL2W = glm::inverse(referenceL2W);

        for (size_t i = 0; i < pointInputs.size(); ++i) {
            const PointData* input = pointInputs[i];
            glm::mat4 inputL2W = toMat4(pointMatrices[i]);
            glm::mat4 inputToReference = invReferenceL2W * inputL2W;

            for (const auto& pos : input->positions) {
                payload.positions.push_back(inputToReference * pos);
            }

            const glm::vec3& minimum = input->domainMinimum;
            const glm::vec3& maximum = input->domainMaximum;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec4 point(
                    (corner & 1u) ? maximum.x : minimum.x,
                    (corner & 2u) ? maximum.y : minimum.y,
                    (corner & 4u) ? maximum.z : minimum.z,
                    1.0f);
                const glm::vec3 transformed = glm::vec3(inputToReference * point);
                payload.domainMinimum = glm::min(payload.domainMinimum, transformed);
                payload.domainMaximum = glm::max(payload.domainMaximum, transformed);
            }
        }

        const uint64_t payloadKey = NodeSocketKey(eval.node.id, eval.node.outputs[0].id);
        outputValue.dataType = payloadtypes::Points;
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, payload, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);

    } else if (dataType == payloadtypes::Geometry || dataType == payloadtypes::Remesh) {
        std::vector<const GeometryData*> geometryInputs;
        std::vector<std::array<float, 16>> geometryMatrices;
        for (const NodeDataBlock* block : inputs) {
            if (block &&
                (block->dataType == payloadtypes::Geometry || block->dataType == payloadtypes::Remesh) &&
                block->payloadHandle.key != 0) {
                const GeometryData* geometryData = payloadRegistry->resolveGeometry(block->payloadHandle);
                std::array<float, 16> localToWorld{};
                if (geometryData && !geometryData->pointPositions.empty() &&
                    payloadRegistry->resolveLocalToWorld(block->payloadHandle, localToWorld)) {
                    geometryInputs.push_back(geometryData);
                    geometryMatrices.push_back(localToWorld);
                }
            }
        }

        if (geometryInputs.empty()) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            return;
        }

        GeometryData payload{};
        payload.localToWorld = geometryMatrices[0];
        payload.baseModelPath = geometryInputs[0]->baseModelPath;

        glm::mat4 referenceL2W = toMat4(payload.localToWorld);
        glm::mat4 invReferenceL2W = glm::inverse(referenceL2W);

        std::vector<GeometryGroup> mergedGroups;
        std::vector<uint32_t> mergedTriangleGroupIds;
        uint32_t nextGroupId = 1;

        for (size_t i = 0; i < geometryInputs.size(); ++i) {
            const GeometryData* input = geometryInputs[i];
            glm::mat4 inputL2W = toMat4(geometryMatrices[i]);
            glm::mat4 inputToReference = invReferenceL2W * inputL2W;

            const uint32_t vertexOffset = static_cast<uint32_t>(payload.pointPositions.size() / 3);

            // Merge vertex positions
            for (size_t vIdx = 0; vIdx + 2 < input->pointPositions.size(); vIdx += 3) {
                glm::vec4 localPos(input->pointPositions[vIdx], input->pointPositions[vIdx+1], input->pointPositions[vIdx+2], 1.0f);
                glm::vec4 refPos = inputToReference * localPos;
                payload.pointPositions.push_back(refPos.x);
                payload.pointPositions.push_back(refPos.y);
                payload.pointPositions.push_back(refPos.z);
            }

            // Merge triangle indices
            for (uint32_t idx : input->triangleIndices) {
                payload.triangleIndices.push_back(idx + vertexOffset);
            }

            // Merge groups
            std::unordered_map<uint32_t, uint32_t> groupIdMap;
            groupIdMap[0] = 0; // 0 stays 0
            for (const auto& inputGroup : input->groups) {
                uint32_t targetId = 0;
                for (const auto& existing : mergedGroups) {
                    if (existing.name == inputGroup.name) {
                        targetId = existing.id;
                        break;
                    }
                }

                if (targetId == 0) {
                    targetId = nextGroupId++;
                    GeometryGroup newGroup = inputGroup;
                    newGroup.id = targetId;
                    mergedGroups.push_back(newGroup);
                }

                groupIdMap[inputGroup.id] = targetId;
            }

            // Merge triangle group IDs
            for (uint32_t gId : input->triangleGroupIds) {
                auto it = groupIdMap.find(gId);
                mergedTriangleGroupIds.push_back(it != groupIdMap.end() ? it->second : 0);
            }
        }

        payload.groups = mergedGroups;
        payload.triangleGroupIds = mergedTriangleGroupIds;

        const uint64_t payloadKey = NodeSocketKey(eval.node.id, eval.node.outputs[0].id);
        outputValue.dataType = payloadtypes::Geometry;
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, payload, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeMerge::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Merge);
    HashNodeCache::combineSocketList(hashValue, hash, "Geometry", HashDomain::Geometry);

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}
