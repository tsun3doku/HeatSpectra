#include "NodeModel.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodeModelParams.hpp"
#include "NodePayloadRegistry.hpp"
#include "scene/MeshImporter.hpp"

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

    MeshImporter::Mesh importedMesh;
    if (!MeshImporter::loadMesh(modelPath, importedMesh) || !importedMesh.isValid()) {
        return false;
    }

    geometry.pointPositions = importedMesh.positions;
    geometry.triangleIndices.reserve(importedMesh.triangleCornerIndices.size());
    for (uint32_t cornerIndex : importedMesh.triangleCornerIndices) {
        geometry.triangleIndices.push_back(importedMesh.corners[cornerIndex].vertexIndex);
    }
    geometry.triangleGroupIds = importedMesh.triangleGroupIds;

    geometry.groups.reserve(importedMesh.groups.size());
    for (const auto& group : importedMesh.groups) {
        GeometryGroup g{};
        g.id = group.id;
        g.name = group.name;
        g.source = group.source;
        geometry.groups.push_back(std::move(g));
    }

    return !geometry.triangleIndices.empty();
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
