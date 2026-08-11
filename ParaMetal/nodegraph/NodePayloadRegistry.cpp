#include "NodePayloadRegistry.hpp"

#include "domain/GeometryData.hpp"
#include "domain/HeatModelData.hpp"
#include "domain/PointData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/TransformData.hpp"

void NodePayloadRegistry::erase(uint64_t key) {
    entries.erase(key);
}

void NodePayloadRegistry::clear() {
    entries.clear();
}

const GeometryData* NodePayloadRegistry::resolveGeometry(const NodeDataHandle& handle, NodeDataHandle* outGeometryHandle) const {
    if (handle.key == 0) {
        return nullptr;
    }

    if (const GeometryData* geometry = get<GeometryData>(handle)) {
        if (outGeometryHandle) *outGeometryHandle = handle;
        return geometry;
    }
    if (const TransformData* transform = get<TransformData>(handle)) {
        return resolveGeometry(transform->sourceHandle, outGeometryHandle);
    }
    if (const RemeshData* remesh = get<RemeshData>(handle)) {
        return resolveGeometry(remesh->sourceMeshHandle, outGeometryHandle);
    }
    if (const HeatModelData* heatModel = get<HeatModelData>(handle)) {
        return resolveGeometry(heatModel->meshHandle, outGeometryHandle);
    }
    return nullptr;
}

const RemeshData* NodePayloadRegistry::resolveRemesh(
    const NodeDataHandle& handle,
    NodeDataHandle* outRemeshHandle) const {
    if (handle.key == 0) return nullptr;

    if (const RemeshData* remesh = get<RemeshData>(handle)) {
        if (outRemeshHandle) *outRemeshHandle = handle;
        return remesh;
    }
    if (const HeatModelData* heatModel = get<HeatModelData>(handle)) {
        return resolveRemesh(heatModel->meshHandle, outRemeshHandle);
    }
    return nullptr;
}

const PointData* NodePayloadRegistry::resolvePoints(const NodeDataHandle& handle, NodeDataHandle* outPointHandle) const {
    if (handle.key == 0) {
        return nullptr;
    }

    if (const PointData* points = get<PointData>(handle)) {
        if (outPointHandle) *outPointHandle = handle;
        return points;
    }
    if (const TransformData* transform = get<TransformData>(handle)) {
        return resolvePoints(transform->sourceHandle, outPointHandle);
    }
    return nullptr;
}

bool NodePayloadRegistry::resolveLocalToWorld(
    const NodeDataHandle& handle,
    std::array<float, 16>& outLocalToWorld) const {
    if (handle.key == 0) return false;

    if (const TransformData* transform = get<TransformData>(handle)) {
        outLocalToWorld = transform->localToWorld;
        return true;
    }
    if (const GeometryData* geometry = get<GeometryData>(handle)) {
        outLocalToWorld = geometry->localToWorld;
        return true;
    }
    if (const PointData* points = get<PointData>(handle)) {
        outLocalToWorld = points->localToWorld;
        return true;
    }
    if (const RemeshData* remesh = get<RemeshData>(handle)) {
        return resolveLocalToWorld(remesh->sourceMeshHandle, outLocalToWorld);
    }
    if (const HeatModelData* heatModel = get<HeatModelData>(handle)) {
        return resolveLocalToWorld(heatModel->meshHandle, outLocalToWorld);
    }
    return false;
}

const HashValues* NodePayloadRegistry::findHashes(const NodeDataHandle& handle) const {
    const auto it = entries.find(handle.key);
    return it != entries.end() ? &it->second.hashes : nullptr;
}

uint64_t NodePayloadRegistry::resolveHash(const NodeDataHandle& handle, HashDomain domain) const {
    if (handle.key == 0) {
        return 0;
    }

    const auto it = entries.find(handle.key);
    return it != entries.end() ? it->second.hashes.get(domain) : 0;
}
