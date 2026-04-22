#include "NodePayloadRegistry.hpp"

#include "NodeGraphHash.hpp"
#include "domain/ContactData.hpp"
#include "domain/GeometryData.hpp"
#include "domain/HeatData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/VoronoiData.hpp"

void GeometryData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combineString(hash, baseModelPath);
    for (float value : localToWorld) {
        NodeGraphHash::combineFloat(hash, value);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(pointPositions.size()));
    for (float value : pointPositions) {
        NodeGraphHash::combineFloat(hash, value);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(triangleIndices.size()));
    for (uint32_t value : triangleIndices) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(triangleGroupIds.size()));
    for (uint32_t value : triangleGroupIds) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(value));
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(groups.size()));
    for (const GeometryGroup& group : groups) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(group.id));
        NodeGraphHash::combineString(hash, group.name);
        NodeGraphHash::combineString(hash, group.source);
    }
    payloadHash = hash;
}

void RemeshData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, static_cast<uint64_t>(active ? 1u : 0u));
    NodeGraphHash::combine(hash, sourcePayloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(params.iterations));
    NodeGraphHash::combineFloat(hash, params.minAngleDegrees);
    NodeGraphHash::combineFloat(hash, params.maxEdgeLength);
    NodeGraphHash::combineFloat(hash, params.stepSize);
    payloadHash = hash;
}

void HeatSourceData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, meshPayloadHash);
    NodeGraphHash::combineFloat(hash, temperature);
    payloadHash = hash;
}

void HeatReceiverData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, meshPayloadHash);
    payloadHash = hash;
}

void HeatData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, voronoiPayloadHash);
    NodeGraphHash::combine(hash, contactPayloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(active ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(paused ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(resetRequested ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(materialBindings.size()));
    for (const HeatMaterialBinding& binding : materialBindings) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(binding.receiverModelNodeId));
        NodeGraphHash::combine(hash, static_cast<uint64_t>(binding.presetId));
    }
    payloadHash = hash;
}

void ContactData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, static_cast<uint64_t>(active ? 1u : 0u));
    NodeGraphHash::combine(hash, emitterPayloadHash);
    NodeGraphHash::combine(hash, receiverPayloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(pair.type));
    NodeGraphHash::combineFloat(hash, pair.minNormalDot);
    NodeGraphHash::combineFloat(hash, pair.contactRadius);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(pair.hasValidContact ? 1u : 0u));
    payloadHash = hash;
}

void VoronoiData::sealPayload() {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, static_cast<uint64_t>(active ? 1u : 0u));
    NodeGraphHash::combineFloat(hash, params.cellSize);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(params.voxelResolution));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(receiverPayloadHashes.size()));
    for (uint64_t receiverPayloadHash : receiverPayloadHashes) {
        NodeGraphHash::combine(hash, receiverPayloadHash);
    }
    payloadHash = hash;
}

void NodePayloadRegistry::erase(uint64_t key) {
    entries.erase(key);
}

void NodePayloadRegistry::clear() {
    entries.clear();
}

const GeometryData* NodePayloadRegistry::resolveGeometryHandle(const NodeDataHandle& handle) const {
    if (handle.key == 0) {
        return nullptr;
    }

    return get<GeometryData>(handle);
}

bool NodePayloadRegistry::hasRemeshHandle(const NodeDataHandle& handle) const {
    if (handle.key == 0) {
        return false;
    }

    return get<RemeshData>(handle) != nullptr;
}

const GeometryData* NodePayloadRegistry::resolveGeometry(NodePayloadType type, const NodeDataHandle& handle) const {
    if (handle.key == 0) {
        return nullptr;
    }

    switch (type) {
    case NodePayloadType::Geometry:
        return resolveGeometryHandle(handle);
    case NodePayloadType::Remesh: {
        const RemeshData* remesh = get<RemeshData>(handle);
        if (!remesh) {
            return nullptr;
        }
        return resolveGeometryHandle(remesh->sourceMeshHandle);
    }
    case NodePayloadType::HeatReceiver: {
        const HeatReceiverData* heatReceiver = get<HeatReceiverData>(handle);
        if (!heatReceiver) {
            return nullptr;
        }
        const NodePayloadType meshType = get<RemeshData>(heatReceiver->meshHandle) != nullptr
            ? NodePayloadType::Remesh
            : NodePayloadType::Geometry;
        return resolveGeometry(meshType, heatReceiver->meshHandle);
    }
    case NodePayloadType::HeatSource: {
        const HeatSourceData* heatSource = get<HeatSourceData>(handle);
        if (!heatSource) {
            return nullptr;
        }
        const NodePayloadType meshType = get<RemeshData>(heatSource->meshHandle) != nullptr
            ? NodePayloadType::Remesh
            : NodePayloadType::Geometry;
        return resolveGeometry(meshType, heatSource->meshHandle);
    }
    default:
        return nullptr;
    }
}

uint64_t NodePayloadRegistry::resolvePayloadHash(NodePayloadType type, const NodeDataHandle& handle) const {
    if (handle.key == 0) {
        return 0;
    }

    switch (type) {
    case NodePayloadType::Geometry: {
        const GeometryData* payload = get<GeometryData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Remesh: {
        const RemeshData* payload = get<RemeshData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::HeatReceiver: {
        const HeatReceiverData* payload = get<HeatReceiverData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::HeatSource: {
        const HeatSourceData* payload = get<HeatSourceData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Contact: {
        const ContactData* payload = get<ContactData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Heat: {
        const HeatData* payload = get<HeatData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::Voronoi: {
        const VoronoiData* payload = get<VoronoiData>(handle);
        return payload ? payload->payloadHash : 0;
    }
    case NodePayloadType::None:
    default:
        return 0;
    }
}
