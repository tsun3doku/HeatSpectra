#include "NodeModel.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeModelParams.hpp"
#include "NodePayloadRegistry.hpp"

#include <tiny_obj_loader.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

const char* NodeModel::typeId() const {
    return nodegraphtypes::Model;
}

void NodeModel::execute(NodeKernelEval& eval) const {
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;

    const ModelNodeParams params = readModelNodeParams(eval.node);
    const std::string& modelPath = params.path;
    GeometryData geometry{};
    bool hasGeometry = false;
    if (!modelPath.empty() && std::filesystem::exists(modelPath)) {
        hasGeometry = loadGeometryFromModelPath(modelPath, geometry);
        geometry.baseModelPath = modelPath;
    }

    for (std::size_t outputIndex = 0;
         outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size();
         ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry ||
            outputValue.dataType != payloadtypes::Geometry ||
            !hasGeometry) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        const uint64_t payloadKey = NodeSocketKey(eval.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, geometry, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeModel::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Model);
    HashBuilder::combineString(hashValue, readModelNodeParams(hash.node).path);

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}

bool NodeModel::parseObjGeometry(const std::string& modelPath, GeometryData& geometry) {
    geometry = {};

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, modelPath.c_str())) {
        return false;
    }

    if (attrib.vertices.empty()) {
        return false;
    }

    geometry.pointPositions = attrib.vertices;
    geometry.triangleIndices.clear();
    geometry.triangleGroupIds.clear();
    geometry.groups.clear();

    const std::size_t pointCount = geometry.pointPositions.size() / 3;
    std::unordered_map<std::string, uint32_t> groupIdByKey;

    const auto getGroupIdForFace = [&](const std::string& shapeName, int materialId) -> uint32_t {
        std::string materialName;
        if (materialId >= 0 && static_cast<std::size_t>(materialId) < materials.size()) {
            materialName = materials[static_cast<std::size_t>(materialId)].name;
        }

        std::string key;
        std::string name;
        std::string source;
        if (!shapeName.empty()) {
            key = "shape:" + shapeName;
            name = shapeName;
            source = "obj.shape";
        }

        if (!materialName.empty()) {
            if (key.empty()) {
                key = "material:" + materialName;
                name = materialName;
                source = "obj.material";
            } else {
                key += "|material:" + materialName;
                name += " [" + materialName + "]";
                source = "obj.shape_material";
            }
        }

        if (key.empty()) {
            key = "default";
            name = "Default";
            source = "generated";
        }

        const auto existingIt = groupIdByKey.find(key);
        if (existingIt != groupIdByKey.end()) {
            return existingIt->second;
        }

        const uint32_t groupId = static_cast<uint32_t>(geometry.groups.size());
        GeometryGroup group{};
        group.id = groupId;
        group.name = name;
        group.source = source;
        geometry.groups.push_back(std::move(group));
        groupIdByKey.emplace(key, groupId);
        return groupId;
    };

    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t indexOffset = 0;
        std::size_t faceIndex = 0;
        for (unsigned char faceVertexCount : shape.mesh.num_face_vertices) {
            const std::size_t faceVertexCountSize = static_cast<std::size_t>(faceVertexCount);
            if (indexOffset + faceVertexCountSize > shape.mesh.indices.size()) {
                break;
            }

            const int materialId =
                (faceIndex < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[faceIndex] : -1;
            const uint32_t groupId = getGroupIdForFace(shape.name, materialId);

            if (faceVertexCount < 3) {
                indexOffset += faceVertexCount;
                ++faceIndex;
                continue;
            }

            const tinyobj::index_t& firstCorner = shape.mesh.indices[indexOffset];
            if (firstCorner.vertex_index < 0 ||
                static_cast<std::size_t>(firstCorner.vertex_index) >= pointCount) {
                indexOffset += faceVertexCount;
                ++faceIndex;
                continue;
            }

            for (unsigned char cornerIndex = 1; cornerIndex + 1 < faceVertexCount; ++cornerIndex) {
                const tinyobj::index_t& secondCorner = shape.mesh.indices[indexOffset + cornerIndex];
                const tinyobj::index_t& thirdCorner = shape.mesh.indices[indexOffset + cornerIndex + 1];
                if (secondCorner.vertex_index < 0 || thirdCorner.vertex_index < 0) {
                    continue;
                }

                const std::size_t secondVertexIndex = static_cast<std::size_t>(secondCorner.vertex_index);
                const std::size_t thirdVertexIndex = static_cast<std::size_t>(thirdCorner.vertex_index);
                if (secondVertexIndex >= pointCount || thirdVertexIndex >= pointCount) {
                    continue;
                }

                geometry.triangleIndices.push_back(static_cast<uint32_t>(firstCorner.vertex_index));
                geometry.triangleIndices.push_back(static_cast<uint32_t>(secondCorner.vertex_index));
                geometry.triangleIndices.push_back(static_cast<uint32_t>(thirdCorner.vertex_index));
                geometry.triangleGroupIds.push_back(groupId);
            }

            indexOffset += faceVertexCountSize;
            ++faceIndex;
        }
    }

    if (geometry.triangleIndices.empty()) {
        return false;
    }

    return true;
}

bool NodeModel::loadGeometryFromModelPath(const std::string& modelPath, GeometryData& geometry) {
    static std::unordered_map<std::string, GeometryData> cachedGeometryByPath;
    static std::unordered_set<std::string> failedGeometryByPath;

    for (const std::string& candidatePath : resolveCandidateModelPaths(modelPath)) {
        if (const auto cacheIt = cachedGeometryByPath.find(candidatePath);
            cacheIt != cachedGeometryByPath.end()) {
            geometry = cacheIt->second;
            return true;
        }

        if (failedGeometryByPath.find(candidatePath) != failedGeometryByPath.end()) {
            continue;
        }

        GeometryData candidateGeometry;
        if (!parseObjGeometry(candidatePath, candidateGeometry)) {
            failedGeometryByPath.insert(candidatePath);
            continue;
        }

        geometry = candidateGeometry;
        cachedGeometryByPath.emplace(candidatePath, std::move(candidateGeometry));
        return true;
    }

    geometry = {};
    return false;
}

std::vector<std::string> NodeModel::resolveCandidateModelPaths(const std::string& modelPath) {
    std::vector<std::string> candidates;
    if (modelPath.empty()) {
        return candidates;
    }

    std::unordered_set<std::string> seenPaths;
    auto addCandidate = [&](const std::filesystem::path& path) {
        const std::string candidate = path.lexically_normal().string();
        if (!candidate.empty() && seenPaths.insert(candidate).second) {
            candidates.push_back(candidate);
        }
    };

    const std::filesystem::path rawPath(modelPath);
    addCandidate(rawPath);

    if (!rawPath.is_absolute()) {
        const std::filesystem::path currentPath = std::filesystem::current_path();
        addCandidate(currentPath / rawPath);
        addCandidate(currentPath / "ParaMetal" / rawPath);
    }

    return candidates;
}
