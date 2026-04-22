#include "RuntimeHandleResolver.hpp"

#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <optional>

std::optional<GeometryData> geometryFromHandle(
    const NodePayloadRegistry* payloadRegistry,
    const NodeDataHandle& meshHandle) {
    if (!payloadRegistry || meshHandle.key == 0) {
        return std::nullopt;
    }

    if (const GeometryData* geometry = payloadRegistry->resolveGeometryHandle(meshHandle)) {
        return *geometry;
    }

    if (const RemeshData* remesh = payloadRegistry->get<RemeshData>(meshHandle)) {
        if (const GeometryData* geometry = payloadRegistry->resolveGeometryHandle(remesh->sourceMeshHandle)) {
            return *geometry;
        }
    }

    return std::nullopt;
}

ProductHandle modelProductFromHandle(
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& ecsRegistry,
    const NodeDataHandle& meshHandle) {
    if (!payloadRegistry || meshHandle.key == 0) {
        return {};
    }

    if (payloadRegistry->resolveGeometryHandle(meshHandle)) {
        return getPublishedHandle<ModelProduct>(ecsRegistry, meshHandle.key);
    }

    const RemeshData* remesh = payloadRegistry->get<RemeshData>(meshHandle);
    if (remesh && remesh->sourceMeshHandle.key != 0) {
        return modelProductFromHandle(
            payloadRegistry,
            ecsRegistry,
            remesh->sourceMeshHandle);
    }

    return {};
}

ProductHandle remeshProductFromHandle(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& ecsRegistry,
    const NodeDataHandle& meshHandle) {
    if (!runtimeBridge || !payloadRegistry || meshHandle.key == 0) {
        return {};
    }

    const ProductHandle handle = runtimeBridge->resolveRemeshProductForPayload(meshHandle);
    if (!handle.isValid()) {
        return {};
    }

    return getPublishedHandle<RemeshProduct>(ecsRegistry, handle.outputSocketKey);
}
