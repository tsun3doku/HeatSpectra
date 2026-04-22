#pragma once

#include "runtime/RuntimeECS.hpp"

#include <optional>

class NodeGraphRuntimeBridge;
class NodePayloadRegistry;
struct GeometryData;
struct NodeDataHandle;
struct ProductHandle;

//                                                   [ Invariant:
//                                                     - Pure runtime handle resolution for payload and product lookups
//                                                     - Depends only on NodePayloadRegistry and ECSRegistry
//                                                     - No dependency on NodeGraphEvaluationState or compilation state
//                                                     - Graph evaluation state resolution remains in RuntimePackageCompiler ]

std::optional<GeometryData> geometryFromHandle(
    const NodePayloadRegistry* payloadRegistry,
    const NodeDataHandle& meshHandle);

ProductHandle modelProductFromHandle(
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& ecsRegistry,
    const NodeDataHandle& meshHandle);

ProductHandle remeshProductFromHandle(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& ecsRegistry,
    const NodeDataHandle& meshHandle);
