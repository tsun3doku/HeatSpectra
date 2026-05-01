#include "RuntimePackageCompiler.hpp"

#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeGraphHash.hpp"
#include "nodegraph/NodeContactParams.hpp"
#include "nodegraph/NodeHeatSolveParams.hpp"
#include "nodegraph/NodeRemeshParams.hpp"
#include "nodegraph/NodeVoronoiParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeHandleResolver.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <optional>
#include <set>
#include <unordered_set>

static uint64_t buildModelPackageHash(const ModelPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 1u);
    NodeGraphHash::combine(hash, package.geometry.payloadHash);
    hash = RuntimeProductHash::mixPod(hash, package.localToWorld);
    return hash;
}

static void combineProductHandleHash(uint64_t& hash, const ProductHandle& handle) {
    NodeGraphHash::combine(hash, static_cast<uint64_t>(handle.type));
    NodeGraphHash::combine(hash, handle.outputSocketKey);
}

static uint64_t productContentHashFromHandle(const ECSRegistry& ecsRegistry, const ProductHandle& handle) {
    if (!handle.isValid()) {
        return 0;
    }

    switch (handle.type) {
    case NodeProductType::Model: {
        const ModelProduct* product = tryGetProduct<ModelProduct>(ecsRegistry, handle.outputSocketKey);
        return product ? product->productHash : 0;
    }
    case NodeProductType::Remesh: {
        const RemeshProduct* product = tryGetProduct<RemeshProduct>(ecsRegistry, handle.outputSocketKey);
        return product ? product->productHash : 0;
    }
    case NodeProductType::Voronoi: {
        const VoronoiProduct* product = tryGetProduct<VoronoiProduct>(ecsRegistry, handle.outputSocketKey);
        return product ? product->productHash : 0;
    }
    case NodeProductType::Contact: {
        const ContactProduct* product = tryGetProduct<ContactProduct>(ecsRegistry, handle.outputSocketKey);
        return product ? product->productHash : 0;
    }
    case NodeProductType::Heat: {
        const HeatProduct* product = tryGetProduct<HeatProduct>(ecsRegistry, handle.outputSocketKey);
        return product ? product->productHash : 0;
    }
    default:
        return 0;
    }
}

static void combineProductDependencyHash(uint64_t& hash, const ECSRegistry& ecsRegistry, const ProductHandle& handle) {
    combineProductHandleHash(hash, handle);
    NodeGraphHash::combine(hash, productContentHashFromHandle(ecsRegistry, handle));
}

// Package hashes intentionally combine authored payload state with resolved
// runtime dependency identity. Payload hash answers "did authored graph data
// change?", while ProductHandle identity answers "did an upstream realized
// dependency change?"

static uint64_t buildRemeshPackageHash(const RemeshPackage& package, const ECSRegistry& ecsRegistry) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 2u);
    NodeGraphHash::combine(hash, package.sourceGeometry.payloadHash);
    combineProductDependencyHash(hash, ecsRegistry, package.modelProductHandle);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.iterations));
    NodeGraphHash::combineFloat(hash, package.minAngleDegrees);
    NodeGraphHash::combineFloat(hash, package.maxEdgeLength);
    NodeGraphHash::combineFloat(hash, package.stepSize);
    return hash;
}

static uint64_t buildVoronoiPackageHash(const VoronoiPackage& package, const ECSRegistry& ecsRegistry) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 3u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverModelProducts.size()));
    for (const ProductHandle& handle : package.receiverModelProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    return hash;
}

static uint64_t buildContactPackageHash(const ContactPackage& package, const ECSRegistry& ecsRegistry) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 4u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    combineProductDependencyHash(hash, ecsRegistry, package.emitterModelProduct);
    combineProductDependencyHash(hash, ecsRegistry, package.receiverModelProduct);
    combineProductDependencyHash(hash, ecsRegistry, package.emitterRemeshProduct);
    combineProductDependencyHash(hash, ecsRegistry, package.receiverRemeshProduct);
    return hash;
}

static uint64_t buildHeatPackageHash(const HeatPackage& package, const ECSRegistry& ecsRegistry) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 5u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    combineProductDependencyHash(hash, ecsRegistry, package.voronoiProduct);
    combineProductDependencyHash(hash, ecsRegistry, package.contactProduct);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.sourceModelProducts.size()));
    for (const ProductHandle& handle : package.sourceModelProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.sourceRemeshProducts.size()));
    for (const ProductHandle& handle : package.sourceRemeshProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverModelProducts.size()));
    for (const ProductHandle& handle : package.receiverModelProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        combineProductDependencyHash(hash, ecsRegistry, handle);
    }
    return hash;
}

namespace {

struct OutputValue {
    uint64_t socketKey = 0;
    const NodeDataBlock* block = nullptr;
};

// geometryFromHandle, modelProductFromHandle, remeshProductFromHandle
// live in RuntimeHandleResolver.hpp/.cpp

uint64_t socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

ProductHandle resolveHeatVoronoiInput(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    const ECSRegistry& ecsRegistry) {
    const NodeGraphSocket* volumeSocket = findInputSocket(node, NodeGraphValueType::Volume);
    if (!volumeSocket) {
        return {};
    }

    const auto sourceIt = evaluationState.sourceSocketByInputSocket.find(socketKey(node.id, volumeSocket->id));
    if (sourceIt == evaluationState.sourceSocketByInputSocket.end()) {
        return {};
    }

    const auto outputIt = evaluationState.outputBySocket.find(sourceIt->second);
    if (outputIt == evaluationState.outputBySocket.end() ||
        outputIt->second.status != EvaluatedSocketStatus::Value) {
        return {};
    }

    const NodeDataBlock& inputBlock = outputIt->second.data;
    if (inputBlock.dataType != NodePayloadType::Voronoi || inputBlock.payloadHandle.key == 0) {
        return {};
    }

    return getPublishedHandle<VoronoiProduct>(ecsRegistry, sourceIt->second);
}

ProductHandle resolveHeatContactInput(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    const ECSRegistry& ecsRegistry) {
    const NodeGraphSocket* fieldSocket = findInputSocket(node, NodeGraphValueType::Field);
    if (!fieldSocket) {
        return {};
    }

    const auto sourceIt = evaluationState.sourceSocketByInputSocket.find(socketKey(node.id, fieldSocket->id));
    if (sourceIt == evaluationState.sourceSocketByInputSocket.end()) {
        return {};
    }

    const auto outputIt = evaluationState.outputBySocket.find(sourceIt->second);
    if (outputIt == evaluationState.outputBySocket.end() ||
        outputIt->second.status != EvaluatedSocketStatus::Value) {
        return {};
    }

    const NodeDataBlock& inputBlock = outputIt->second.data;
    if (inputBlock.dataType != NodePayloadType::Contact || inputBlock.payloadHandle.key == 0) {
        return {};
    }

    return getPublishedHandle<ContactProduct>(ecsRegistry, sourceIt->second);
}

std::optional<OutputValue> resolveOutputValue(
    const NodeGraphNode& node,
    const NodeGraphSocket& output,
    const NodeGraphEvaluationState& evaluationState) {
    const uint64_t outputSocketKey = socketKey(node.id, output.id);
    const auto valueIt = evaluationState.outputBySocket.find(outputSocketKey);
    if (valueIt == evaluationState.outputBySocket.end() ||
        valueIt->second.status != EvaluatedSocketStatus::Value ||
        valueIt->second.data.payloadHandle.key == 0) {
        return std::nullopt;
    }

    return OutputValue{outputSocketKey, &valueIt->second.data};
}

template <typename T>
const T* resolvePayload(
    const NodePayloadRegistry* payloadRegistry,
    const std::optional<OutputValue>& outputValue) {
    if (!payloadRegistry || !outputValue.has_value() || !outputValue->block) {
        return nullptr;
    }

    return payloadRegistry->get<T>(outputValue->block->payloadHandle);
}


}

void RuntimePackageCompiler::setRuntimeBridge(const NodeGraphRuntimeBridge* updatedRuntimeBridge) {
    runtimeBridge = updatedRuntimeBridge;
}

ModelPackage RuntimePackageCompiler::buildModelPackage(
    const GeometryData& geometry) const {
    ModelPackage package{};
    package.geometry = geometry;
    package.localToWorld = geometry.localToWorld;
    package.packageHash = buildModelPackageHash(package);
    return package;
}

RemeshPackage RuntimePackageCompiler::buildRemeshPackage(
    const NodeGraphNode& node,
    const RemeshData& remesh,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& registry,
    const NodeDataHandle& remeshHandle) const {
    RemeshPackage package{};
    if (!payloadRegistry || !remesh.active || remesh.sourceMeshHandle.key == 0) {
        return package;
    }

    const GeometryData* sourceGeometry = payloadRegistry->resolveGeometryHandle(remesh.sourceMeshHandle);
    if (!sourceGeometry) {
        return package;
    }

    package.sourceGeometry = *sourceGeometry;
    package.iterations = remesh.iterations;
    package.minAngleDegrees = remesh.minAngleDegrees;
    package.maxEdgeLength = remesh.maxEdgeLength;
    package.stepSize = remesh.stepSize;
    const RemeshNodeParams nodeParams = readRemeshNodeParams(node);
    package.display.showRemeshOverlay = nodeParams.preview.showRemeshOverlay;
    package.display.showFaceNormals = nodeParams.preview.showFaceNormals;
    package.display.showVertexNormals = nodeParams.preview.showVertexNormals;
    package.display.normalLength = static_cast<float>(nodeParams.normalLength);
    package.remeshHandle = remeshHandle;
    package.modelProductHandle = modelProductFromHandle(
        payloadRegistry,
        registry,
        remesh.sourceMeshHandle);
    package.packageHash = buildRemeshPackageHash(package, registry);
    return package;
}

VoronoiPackage RuntimePackageCompiler::buildVoronoiPackage(
    const NodeGraphNode& node,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& registry,
    const VoronoiData& voronoi) const {
    VoronoiPackage package{};
    package.authored = voronoi;

    if (!payloadRegistry || !voronoi.active || voronoi.receiverMeshHandles.empty()) {
        return package;
    }

    package.receiverModelProducts.reserve(voronoi.receiverMeshHandles.size());
    package.receiverRemeshProducts.reserve(voronoi.receiverMeshHandles.size());
    package.receiverLocalToWorlds.reserve(voronoi.receiverMeshHandles.size());

    std::set<NodeDataHandle> seenMeshHandles;
    for (const NodeDataHandle& meshHandle : voronoi.receiverMeshHandles) {
        if (!seenMeshHandles.insert(meshHandle).second) {
            continue;
        }

        const std::optional<GeometryData> receiverGeometry = geometryFromHandle(payloadRegistry, meshHandle);
        if (!receiverGeometry.has_value()) {
            continue;
        }

        const ProductHandle receiverRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, registry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverLocalToWorlds.push_back(receiverGeometry->localToWorld);
        package.receiverModelProducts.push_back(modelProductFromHandle(payloadRegistry, registry, meshHandle));
        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(node);
    package.display.showVoronoi = nodeParams.preview.showVoronoi;
    package.display.showPoints = nodeParams.preview.showPoints;
    package.packageHash = buildVoronoiPackageHash(package, registry);
    return package;
}

HeatPackage RuntimePackageCompiler::buildHeatPackage(
    const NodeGraphNode& node,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& registry,
    const HeatData& heat,
    const ProductHandle& voronoiProduct,
    const ProductHandle& contactProduct) const {
    HeatPackage package{};
    package.authored = heat;
    package.voronoiProduct = voronoiProduct;
    package.contactProduct = contactProduct;

    const HeatSolveNodeParams nodeParams = readHeatSolveNodeParams(node);
    package.display.showHeatOverlay = nodeParams.preview.showHeatOverlay;

    if (!payloadRegistry) {
        return package;
    }

    std::unordered_set<uint64_t> seenSourceRemeshSocketKeys;
    std::set<NodeDataHandle> seenSourceHandles;
    package.sourceModelProducts.reserve(heat.sourceHandles.size());
    package.sourceRemeshProducts.reserve(heat.sourceHandles.size());
    package.sourceTemperatures.reserve(heat.sourceHandles.size());
    for (const NodeDataHandle& sourceHandle : heat.sourceHandles) {
        if (sourceHandle.key == 0) {
            continue;
        }

        const HeatSourceData* heatSource = payloadRegistry->get<HeatSourceData>(sourceHandle);
        if (!heatSource || heatSource->meshHandle.key == 0) {
            continue;
        }
        if (!seenSourceHandles.insert(heatSource->meshHandle).second) {
            continue;
        }

        const std::optional<GeometryData> sourceGeometry = geometryFromHandle(payloadRegistry, heatSource->meshHandle);
        if (!sourceGeometry.has_value()) {
            continue;
        }

        const ProductHandle sourceModelProduct =
            modelProductFromHandle(payloadRegistry, registry, heatSource->meshHandle);
        if (!sourceModelProduct.isValid()) {
            continue;
        }
        const ProductHandle sourceRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, registry, heatSource->meshHandle);
        if (!sourceRemeshProduct.isValid()) {
            continue;
        }
        if (!seenSourceRemeshSocketKeys.insert(sourceRemeshProduct.outputSocketKey).second) {
            continue;
        }

        package.sourceModelProducts.push_back(sourceModelProduct);
        package.sourceRemeshProducts.push_back(sourceRemeshProduct);
        package.sourceTemperatures.push_back(heatSource->temperature);
    }

    std::set<NodeDataHandle> seenReceiverHandles;
    package.receiverModelProducts.reserve(heat.receiverMeshHandles.size());
    package.receiverRemeshProducts.reserve(heat.receiverMeshHandles.size());
    for (const NodeDataHandle& meshHandle : heat.receiverMeshHandles) {
        if (!seenReceiverHandles.insert(meshHandle).second) {
            continue;
        }

        const std::optional<GeometryData> receiverGeometry = geometryFromHandle(payloadRegistry, meshHandle);
        if (!receiverGeometry.has_value()) {
            continue;
        }

        const ProductHandle receiverModelProduct =
            modelProductFromHandle(payloadRegistry, registry, meshHandle);
        if (!receiverModelProduct.isValid()) {
            continue;
        }
        const ProductHandle receiverRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, registry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverModelProducts.push_back(receiverModelProduct);
        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    package.packageHash = buildHeatPackageHash(package, registry);
    return package;
}

ContactPackage RuntimePackageCompiler::buildContactPackage(
    const NodeGraphNode& node,
    const NodePayloadRegistry* payloadRegistry,
    const ECSRegistry& registry,
    const ContactData& contact) const {
    ContactPackage package{};
    package.authored = contact;
    const ContactNodeParams nodeParams = readContactNodeParams(node);
    package.display.showContactLines = nodeParams.preview.showContactLines;
    if (!payloadRegistry || !contact.active || !contact.pair.hasValidContact) {
        return package;
    }

    const ContactPairData& pair = contact.pair;
    if (pair.endpointA.meshHandle.key == 0 ||
        pair.endpointB.meshHandle.key == 0 ||
        pair.endpointA.meshHandle == pair.endpointB.meshHandle) {
        return package;
    }

    const ContactPairEndpoint& emitterEndpoint = pair.endpointA;
    const ContactPairEndpoint& receiverEndpoint = pair.endpointB;

    const std::optional<GeometryData> emitterGeometry = geometryFromHandle(payloadRegistry, emitterEndpoint.meshHandle);
    const std::optional<GeometryData> receiverGeometry = geometryFromHandle(payloadRegistry, receiverEndpoint.meshHandle);
    if (!emitterGeometry.has_value() || !receiverGeometry.has_value()) {
        return package;
    }

    package.emitterLocalToWorld = emitterGeometry->localToWorld;
    package.receiverLocalToWorld = receiverGeometry->localToWorld;
    package.emitterModelProduct = modelProductFromHandle(payloadRegistry, registry, emitterEndpoint.meshHandle);
    package.receiverModelProduct = modelProductFromHandle(payloadRegistry, registry, receiverEndpoint.meshHandle);
    package.emitterRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, registry, emitterEndpoint.meshHandle);
    package.receiverRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, registry, receiverEndpoint.meshHandle);

    package.packageHash = buildContactPackageHash(package, registry);
    return package;
}

template <typename PackageT>
void RuntimePackageCompiler::applyPackage(ECSRegistry& registry, uint64_t socketKey, const PackageT& pkg, std::unordered_set<ECSEntity>& staleEntities) {
    auto entity = static_cast<ECSEntity>(socketKey);
    if (!registry.valid(entity)) {
        static_cast<void>(registry.create(entity));
    }
    if (registry.all_of<PackageT>(entity)) {
        if (!registry.get<PackageT>(entity).matches(pkg)) {
            registry.replace<PackageT>(entity, pkg);
        }
    } else {
        registry.emplace<PackageT>(entity, pkg);
    }
    staleEntities.erase(entity);
}

void RuntimePackageCompiler::compileAndApply(
    const NodeGraphState& graphState,
    const NodeGraphEvaluationState& evaluationState,
    const NodePayloadRegistry* payloadRegistry,
    ECSRegistry& registry,
    std::unordered_set<ECSEntity>& staleEntities) const {

    for (const NodeGraphNode& node : graphState.nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedPayloadType == NodePayloadType::None) {
                continue;
            }

            const std::optional<OutputValue> outputValue = resolveOutputValue(node, output, evaluationState);
            if (!outputValue.has_value()) {
                continue;
            }

            switch (output.contract.producedPayloadType) {
            case NodePayloadType::Geometry: {
                const GeometryData* geometry = resolvePayload<GeometryData>(payloadRegistry, outputValue);
                if (!geometry) {
                    break;
                }

                ModelPackage package = buildModelPackage(*geometry);
                applyPackage<ModelPackage>(registry, outputValue->socketKey, package, staleEntities);
                break;
            }
            case NodePayloadType::Remesh: {
                const RemeshData* remesh = resolvePayload<RemeshData>(payloadRegistry, outputValue);
                if (!remesh || !remesh->active || remesh->sourceMeshHandle.key == 0) {
                    break;
                }

                RemeshPackage package =
                    buildRemeshPackage(node, *remesh, payloadRegistry, registry, outputValue->block->payloadHandle);
                applyPackage<RemeshPackage>(registry, outputValue->socketKey, package, staleEntities);
                break;
            }
            case NodePayloadType::Voronoi: {
                const VoronoiData* voronoi = resolvePayload<VoronoiData>(payloadRegistry, outputValue);
                if (!voronoi) {
                    break;
                }

                VoronoiPackage package = buildVoronoiPackage(node, payloadRegistry, registry, *voronoi);
                applyPackage<VoronoiPackage>(registry, outputValue->socketKey, package, staleEntities);
                break;
            }
            case NodePayloadType::Contact: {
                const ContactData* contact = resolvePayload<ContactData>(payloadRegistry, outputValue);
                if (!contact) {
                    break;
                }

                ContactPackage package = buildContactPackage(node, payloadRegistry, registry, *contact);
                applyPackage<ContactPackage>(registry, outputValue->socketKey, package, staleEntities);
                break;
            }
            case NodePayloadType::Heat: {
                const HeatData* heat = resolvePayload<HeatData>(payloadRegistry, outputValue);
                if (!heat) {
                    break;
                }

                const ProductHandle voronoiProduct =
                    resolveHeatVoronoiInput(node, evaluationState, registry);
                const ProductHandle contactProduct =
                    resolveHeatContactInput(node, evaluationState, registry);
                HeatPackage package = buildHeatPackage(
                    node,
                    payloadRegistry,
                    registry,
                    *heat,
                    voronoiProduct,
                    contactProduct);
                applyPackage<HeatPackage>(registry, outputValue->socketKey, package, staleEntities);
                break;
            }
            default:
                break;
            }
        }
    }
}
