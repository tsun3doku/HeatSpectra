#include "RuntimePackageCompiler.hpp"

#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodeGraphTopology.hpp"
#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeGraphHash.hpp"
#include "nodegraph/NodeContactParams.hpp"
#include "nodegraph/NodeHeatSolveParams.hpp"
#include "nodegraph/NodeRemeshParams.hpp"
#include "nodegraph/NodeVoronoiParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

#include <algorithm>
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
    NodeGraphHash::combine(hash, handle.outputRevision);
}

// Package hashes intentionally combine authored payload state with resolved
// runtime dependency identity. Payload hash answers "did authored graph data
// change?", while ProductHandle identity answers "did an upstream realized
// dependency change?" RuntimePackageSync compares only packageHash.

static uint64_t buildRemeshPackageHash(const RemeshPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 2u);
    NodeGraphHash::combine(hash, package.sourceGeometry.payloadHash);
    combineProductHandleHash(hash, package.modelProductHandle);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.params.iterations));
    NodeGraphHash::combineFloat(hash, package.params.minAngleDegrees);
    NodeGraphHash::combineFloat(hash, package.params.maxEdgeLength);
    NodeGraphHash::combineFloat(hash, package.params.stepSize);
    return hash;
}

static uint64_t buildRemeshDisplayPackageHash(const RemeshPackage& package) {
    uint64_t hash = package.packageHash;
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showRemeshOverlay ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showFaceNormals ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showVertexNormals ? 1u : 0u));
    NodeGraphHash::combineFloat(hash, package.display.normalLength);
    return hash;
}

static uint64_t buildVoronoiPackageHash(const VoronoiPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 3u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverModelProducts.size()));
    for (const ProductHandle& handle : package.receiverModelProducts) {
        combineProductHandleHash(hash, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        combineProductHandleHash(hash, handle);
    }
    return hash;
}

static uint64_t buildVoronoiDisplayPackageHash(const VoronoiPackage& package) {
    uint64_t hash = package.packageHash;
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showVoronoi ? 1u : 0u));
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showPoints ? 1u : 0u));
    return hash;
}

static uint64_t buildContactPackageHash(const ContactPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 4u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    combineProductHandleHash(hash, package.emitterModelProduct);
    combineProductHandleHash(hash, package.receiverModelProduct);
    combineProductHandleHash(hash, package.emitterRemeshProduct);
    combineProductHandleHash(hash, package.receiverRemeshProduct);
    return hash;
}

static uint64_t buildContactDisplayPackageHash(const ContactPackage& package) {
    uint64_t hash = package.packageHash;
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showContactLines ? 1u : 0u));
    return hash;
}

static uint64_t buildHeatPackageHash(const HeatPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 5u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    combineProductHandleHash(hash, package.voronoiProduct);
    combineProductHandleHash(hash, package.contactProduct);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.sourceModelProducts.size()));
    for (const ProductHandle& handle : package.sourceModelProducts) {
        combineProductHandleHash(hash, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.sourceRemeshProducts.size()));
    for (const ProductHandle& handle : package.sourceRemeshProducts) {
        combineProductHandleHash(hash, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverModelProducts.size()));
    for (const ProductHandle& handle : package.receiverModelProducts) {
        combineProductHandleHash(hash, handle);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        combineProductHandleHash(hash, handle);
    }
    return hash;
}

static uint64_t buildHeatDisplayPackageHash(const HeatPackage& package) {
    uint64_t hash = package.packageHash;
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.display.showHeatOverlay ? 1u : 0u));
    return hash;
}

namespace {

struct OutputValue {
    uint64_t socketKey = 0;
    const NodeDataBlock* block = nullptr;
};

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

uint64_t socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

ProductHandle remeshProductFromHandle(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const RuntimeProductRegistry* runtimeProductRegistry,
    const NodeDataHandle& meshHandle) {
    if (!runtimeBridge || !payloadRegistry || !runtimeProductRegistry || meshHandle.key == 0) {
        return {};
    }

    const ProductHandle handle = runtimeBridge->resolveRemeshProductForPayload(meshHandle);
    if (!handle.isValid()) {
        return {};
    }

    return runtimeProductRegistry->getPublishedHandle(NodeProductType::Remesh, handle.outputSocketKey);
}

ProductHandle modelProductFromHandle(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const RuntimeProductRegistry* runtimeProductRegistry,
    const NodeDataHandle& meshHandle) {
    if (!payloadRegistry || !runtimeProductRegistry || meshHandle.key == 0) {
        return {};
    }

    if (payloadRegistry->resolveGeometryHandle(meshHandle)) {
        return runtimeProductRegistry->getPublishedHandle(NodeProductType::Model, meshHandle.key);
    }

    const RemeshData* remesh = payloadRegistry->get<RemeshData>(meshHandle);
    if (remesh && remesh->sourceMeshHandle.key != 0) {
        return modelProductFromHandle(
            runtimeBridge,
            payloadRegistry,
            runtimeProductRegistry,
            remesh->sourceMeshHandle);
    }

    return {};
}

ProductHandle resolveHeatVoronoiInput(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    const RuntimeProductRegistry* runtimeProductRegistry) {
    if (!runtimeProductRegistry) {
        return {};
    }

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

    return runtimeProductRegistry->getPublishedHandle(NodeProductType::Voronoi, sourceIt->second);
}

ProductHandle resolveHeatContactInput(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    const RuntimeProductRegistry* runtimeProductRegistry) {
    if (!runtimeProductRegistry) {
        return {};
    }

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

    return runtimeProductRegistry->getPublishedHandle(NodeProductType::Contact, sourceIt->second);
}

void appendDependency(
    std::vector<PackageDependency>& dependencies,
    std::unordered_set<uint64_t>& seenDependencyKeys,
    NodeProductType productType,
    uint64_t outputSocketKey) {
    if (productType == NodeProductType::None || outputSocketKey == 0) {
        return;
    }

    const uint64_t dependencyKey =
        (static_cast<uint64_t>(productType) << 56) ^
        outputSocketKey;
    if (!seenDependencyKeys.insert(dependencyKey).second) {
        return;
    }

    PackageDependency dependency{};
    dependency.productType = productType;
    dependency.outputSocketKey = outputSocketKey;
    dependencies.push_back(dependency);
}

uint64_t remeshDependencySocketKey(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodeDataHandle& meshHandle) {
    if (meshHandle.key == 0) {
        return 0;
    }

    const ProductHandle remeshHandle =
        runtimeBridge ? runtimeBridge->resolveRemeshProductForPayload(meshHandle) : ProductHandle{};
    if (remeshHandle.isValid()) {
        return remeshHandle.outputSocketKey;
    }

    return meshHandle.key;
}

std::optional<uint64_t> resolveInputSourceSocketKey(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    NodeGraphValueType inputType,
    NodePayloadType payloadType) {
    const NodeGraphSocket* inputSocket = findInputSocket(node, inputType);
    if (!inputSocket) {
        return std::nullopt;
    }

    const auto sourceIt = evaluationState.sourceSocketByInputSocket.find(socketKey(node.id, inputSocket->id));
    if (sourceIt == evaluationState.sourceSocketByInputSocket.end()) {
        return std::nullopt;
    }

    const auto outputIt = evaluationState.outputBySocket.find(sourceIt->second);
    if (outputIt == evaluationState.outputBySocket.end() ||
        outputIt->second.status != EvaluatedSocketStatus::Value ||
        outputIt->second.data.dataType != payloadType ||
        outputIt->second.data.payloadHandle.key == 0) {
        return std::nullopt;
    }

    return sourceIt->second;
}

PackageNode buildPackageNode(
    PackageKind kind,
    uint64_t outputSocketKey,
    uint64_t packageHash,
    const std::vector<PackageDependency>& dependencies) {
    PackageNode node{};
    node.key.kind = kind;
    node.key.outputSocketKey = outputSocketKey;
    node.dependencies = dependencies;
    node.packageHash = packageHash;
    return node;
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

std::vector<PackageDependency> buildRemeshDeps(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const RuntimeProductRegistry* runtimeProductRegistry,
    const RemeshData& remesh) {
    std::vector<PackageDependency> dependencies;
    std::unordered_set<uint64_t> seenDependencyKeys;

    appendDependency(
        dependencies,
        seenDependencyKeys,
        NodeProductType::Model,
        modelProductFromHandle(
            runtimeBridge,
            payloadRegistry,
            runtimeProductRegistry,
            remesh.sourceMeshHandle).outputSocketKey);

    return dependencies;
}

std::vector<PackageDependency> buildVoronoiDeps(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const VoronoiData& voronoi) {
    std::vector<PackageDependency> dependencies;
    std::unordered_set<uint64_t> seenDependencyKeys;

    for (const NodeDataHandle& meshHandle : voronoi.receiverMeshHandles) {
        appendDependency(
            dependencies,
            seenDependencyKeys,
            NodeProductType::Remesh,
            remeshDependencySocketKey(runtimeBridge, meshHandle));
    }

    return dependencies;
}

std::vector<PackageDependency> buildContactDeps(
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const RuntimeProductRegistry* runtimeProductRegistry,
    const ContactData& contact) {
    std::vector<PackageDependency> dependencies;
    std::unordered_set<uint64_t> seenDependencyKeys;
    if (!contact.pair.hasValidContact) {
        return dependencies;
    }

    appendDependency(
        dependencies,
        seenDependencyKeys,
        NodeProductType::Remesh,
        remeshDependencySocketKey(runtimeBridge, contact.pair.endpointA.meshHandle));
    appendDependency(
        dependencies,
        seenDependencyKeys,
        NodeProductType::Remesh,
        remeshDependencySocketKey(runtimeBridge, contact.pair.endpointB.meshHandle));
    appendDependency(
        dependencies,
        seenDependencyKeys,
        NodeProductType::Model,
        modelProductFromHandle(
            runtimeBridge,
            payloadRegistry,
            runtimeProductRegistry,
            contact.pair.endpointA.meshHandle).outputSocketKey);
    appendDependency(
        dependencies,
        seenDependencyKeys,
        NodeProductType::Model,
        modelProductFromHandle(
            runtimeBridge,
            payloadRegistry,
            runtimeProductRegistry,
            contact.pair.endpointB.meshHandle).outputSocketKey);

    return dependencies;
}

std::vector<PackageDependency> buildHeatDeps(
    const NodeGraphNode& node,
    const NodeGraphEvaluationState& evaluationState,
    const NodeGraphRuntimeBridge* runtimeBridge,
    const NodePayloadRegistry* payloadRegistry,
    const HeatData& heat) {
    std::vector<PackageDependency> dependencies;
    std::unordered_set<uint64_t> seenDependencyKeys;

    const std::optional<uint64_t> voronoiSourceSocketKey =
        resolveInputSourceSocketKey(node, evaluationState, NodeGraphValueType::Volume, NodePayloadType::Voronoi);
    if (voronoiSourceSocketKey.has_value()) {
        appendDependency(dependencies, seenDependencyKeys, NodeProductType::Voronoi, *voronoiSourceSocketKey);
    }

    const std::optional<uint64_t> contactSourceSocketKey =
        resolveInputSourceSocketKey(node, evaluationState, NodeGraphValueType::Field, NodePayloadType::Contact);
    if (contactSourceSocketKey.has_value()) {
        appendDependency(dependencies, seenDependencyKeys, NodeProductType::Contact, *contactSourceSocketKey);
    }

    for (const NodeDataHandle& sourceHandle : heat.sourceHandles) {
        if (sourceHandle.key == 0 || !payloadRegistry) {
            continue;
        }

        const HeatSourceData* source = payloadRegistry->get<HeatSourceData>(sourceHandle);
        if (!source) {
            continue;
        }

        appendDependency(
            dependencies,
            seenDependencyKeys,
            NodeProductType::Remesh,
            remeshDependencySocketKey(runtimeBridge, source->meshHandle));
    }

    for (const NodeDataHandle& receiverMeshHandle : heat.receiverMeshHandles) {
        appendDependency(
            dependencies,
            seenDependencyKeys,
            NodeProductType::Remesh,
            remeshDependencySocketKey(runtimeBridge, receiverMeshHandle));
    }

    return dependencies;
}

}

void RuntimePackageCompiler::setRuntimeBridge(const NodeGraphRuntimeBridge* updatedRuntimeBridge) {
    runtimeBridge = updatedRuntimeBridge;
}

void RuntimePackageCompiler::setRuntimeProductRegistry(const RuntimeProductRegistry* updatedRuntimeProductRegistry) {
    runtimeProductRegistry = updatedRuntimeProductRegistry;
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
    const NodeDataHandle& remeshHandle) const {
    RemeshPackage package{};
    if (!payloadRegistry || !remesh.active || remesh.sourceMeshHandle.key == 0) {
        return package;
    }

    const std::optional<GeometryData> sourceGeometry = geometryFromHandle(payloadRegistry, remesh.sourceMeshHandle);
    if (!sourceGeometry.has_value()) {
        return package;
    }

    package.sourceGeometry = *sourceGeometry;
    package.params = remesh.params;
    const RemeshNodeParams nodeParams = readRemeshNodeParams(node);
    package.display.showRemeshOverlay = nodeParams.preview.showRemeshOverlay;
    package.display.showFaceNormals = nodeParams.preview.showFaceNormals;
    package.display.showVertexNormals = nodeParams.preview.showVertexNormals;
    package.display.normalLength = static_cast<float>(nodeParams.normalLength);
    package.remeshHandle = remeshHandle;
    package.modelProductHandle = modelProductFromHandle(
        runtimeBridge,
        payloadRegistry,
        runtimeProductRegistry,
        remesh.sourceMeshHandle);
    package.packageHash = buildRemeshPackageHash(package);
    package.displayPackageHash = buildRemeshDisplayPackageHash(package);
    return package;
}

VoronoiPackage RuntimePackageCompiler::buildVoronoiPackage(
    const NodeGraphNode& node,
    const NodePayloadRegistry* payloadRegistry,
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
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverLocalToWorlds.push_back(receiverGeometry->localToWorld);
        package.receiverModelProducts.push_back(modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle));
        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    const VoronoiNodeParams nodeParams = readVoronoiNodeParams(node);
    package.display.showVoronoi = nodeParams.preview.showVoronoi;
    package.display.showPoints = nodeParams.preview.showPoints;
    package.packageHash = buildVoronoiPackageHash(package);
    package.displayPackageHash = buildVoronoiDisplayPackageHash(package);
    return package;
}

HeatPackage RuntimePackageCompiler::buildHeatPackage(
    const NodeGraphNode& node,
    const NodeGraphTopology& topology,
    const NodePayloadRegistry* payloadRegistry,
    const HeatData& heat,
    const ProductHandle& voronoiProduct,
    const ProductHandle& contactProduct) const {
    HeatPackage package{};
    package.authored = heat;
    package.voronoiProduct = voronoiProduct;
    package.contactProduct = contactProduct;

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
            modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, heatSource->meshHandle);
        if (!sourceModelProduct.isValid()) {
            continue;
        }
        const ProductHandle sourceRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, heatSource->meshHandle);
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
            modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle);
        if (!receiverModelProduct.isValid()) {
            continue;
        }
        const ProductHandle receiverRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverModelProducts.push_back(receiverModelProduct);
        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    package.runtimeThermalMaterials = buildRuntimeThermalMaterials(
        topology,
        package.receiverRemeshProducts,
        heat.materialBindings);

    const HeatSolveNodeParams nodeParams = readHeatSolveNodeParams(node);
    package.display.showHeatOverlay = nodeParams.preview.showHeatOverlay;
    package.packageHash = buildHeatPackageHash(package);
    package.displayPackageHash = buildHeatDisplayPackageHash(package);
    return package;
}

ContactPackage RuntimePackageCompiler::buildContactPackage(
    const NodeGraphNode& node,
    const NodePayloadRegistry* payloadRegistry,
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

    const ContactPairEndpoint& emitterEndpoint =
        (pair.endpointA.role == ContactPairRole::Source)
        ? pair.endpointA
        : pair.endpointB;
    const ContactPairEndpoint& receiverEndpoint =
        (pair.endpointA.role == ContactPairRole::Source)
        ? pair.endpointB
        : pair.endpointA;

    const std::optional<GeometryData> emitterGeometry = geometryFromHandle(payloadRegistry, emitterEndpoint.meshHandle);
    const std::optional<GeometryData> receiverGeometry = geometryFromHandle(payloadRegistry, receiverEndpoint.meshHandle);
    if (!emitterGeometry.has_value() || !receiverGeometry.has_value()) {
        return package;
    }

    package.emitterLocalToWorld = emitterGeometry->localToWorld;
    package.receiverLocalToWorld = receiverGeometry->localToWorld;
    package.emitterModelProduct = modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, emitterEndpoint.meshHandle);
    package.receiverModelProduct = modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, receiverEndpoint.meshHandle);
    package.emitterRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, emitterEndpoint.meshHandle);
    package.receiverRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, receiverEndpoint.meshHandle);

    package.packageHash = buildContactPackageHash(package);
    package.displayPackageHash = buildContactDisplayPackageHash(package);
    return package;
}

RuntimePackageGraph RuntimePackageCompiler::buildRuntimePackageGraph(
    const NodeGraphState& graphState,
    const NodeGraphEvaluationState& evaluationState,
    const NodePayloadRegistry* payloadRegistry) const {
    RuntimePackageGraph graph{};
    const NodeGraphTopology topology(graphState);

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
                graph.compiledPackages.packageSet.modelBySocket.emplace(outputValue->socketKey, package);
                graph.nodes.push_back(buildPackageNode(
                    PackageKind::Model,
                    outputValue->socketKey,
                    package.packageHash,
                    {}));
                break;
            }
            case NodePayloadType::Remesh: {
                const RemeshData* remesh = resolvePayload<RemeshData>(payloadRegistry, outputValue);
                if (!remesh || !remesh->active || remesh->sourceMeshHandle.key == 0) {
                    break;
                }

                RemeshPackage package =
                    buildRemeshPackage(node, *remesh, payloadRegistry, outputValue->block->payloadHandle);
                graph.compiledPackages.packageSet.remeshBySocket.emplace(outputValue->socketKey, package);

                graph.nodes.push_back(buildPackageNode(
                    PackageKind::Remesh,
                    outputValue->socketKey,
                    package.packageHash,
                    buildRemeshDeps(
                        runtimeBridge,
                        payloadRegistry,
                        runtimeProductRegistry,
                        *remesh)));
                break;
            }
            case NodePayloadType::Voronoi: {
                const VoronoiData* voronoi = resolvePayload<VoronoiData>(payloadRegistry, outputValue);
                if (!voronoi) {
                    break;
                }

                VoronoiPackage package = buildVoronoiPackage(node, payloadRegistry, *voronoi);
                graph.compiledPackages.packageSet.voronoiBySocket.emplace(outputValue->socketKey, package);

                graph.nodes.push_back(buildPackageNode(
                    PackageKind::Voronoi,
                    outputValue->socketKey,
                    package.packageHash,
                    buildVoronoiDeps(runtimeBridge, *voronoi)));
                break;
            }
            case NodePayloadType::Contact: {
                const ContactData* contact = resolvePayload<ContactData>(payloadRegistry, outputValue);
                if (!contact) {
                    break;
                }

                ContactPackage package = buildContactPackage(node, payloadRegistry, *contact);
                graph.compiledPackages.packageSet.contactBySocket.emplace(outputValue->socketKey, package);

                graph.nodes.push_back(buildPackageNode(
                    PackageKind::Contact,
                    outputValue->socketKey,
                    package.packageHash,
                    buildContactDeps(
                        runtimeBridge,
                        payloadRegistry,
                        runtimeProductRegistry,
                        *contact)));
                break;
            }
            case NodePayloadType::Heat: {
                const HeatData* heat = resolvePayload<HeatData>(payloadRegistry, outputValue);
                if (!heat) {
                    break;
                }

                const ProductHandle voronoiProduct =
                    resolveHeatVoronoiInput(node, evaluationState, runtimeProductRegistry);
                const ProductHandle contactProduct =
                    resolveHeatContactInput(node, evaluationState, runtimeProductRegistry);
                HeatPackage package = buildHeatPackage(
                    node,
                    topology,
                    payloadRegistry,
                    *heat,
                    voronoiProduct,
                    contactProduct);
                graph.compiledPackages.packageSet.heatBySocket.emplace(outputValue->socketKey, package);

                graph.nodes.push_back(buildPackageNode(
                    PackageKind::Heat,
                    outputValue->socketKey,
                    package.packageHash,
                    buildHeatDeps(
                        node,
                        evaluationState,
                        runtimeBridge,
                        payloadRegistry,
                        *heat)));
                break;
            }
            default:
                break;
            }
        }
    }

    return graph;
}

std::vector<RuntimeThermalMaterial> RuntimePackageCompiler::buildRuntimeThermalMaterials(
    const NodeGraphTopology& topology,
    const std::vector<ProductHandle>& receiverRemeshProducts,
    const std::vector<HeatMaterialBinding>& materialBindings) const {
    std::unordered_map<uint32_t, HeatMaterialPresetId> presetByNodeModelId;
    for (const HeatMaterialBinding& binding : materialBindings) {
        if (binding.receiverModelNodeId != 0) {
            presetByNodeModelId[binding.receiverModelNodeId] = binding.presetId;
        }
    }

    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
    runtimeThermalMaterials.reserve(receiverRemeshProducts.size());
    std::unordered_set<uint32_t> seenRuntimeModelIds;
    const size_t receiverCount = receiverRemeshProducts.size();
    for (size_t index = 0; index < receiverCount; ++index) {
        const ProductHandle& remeshProductHandle = receiverRemeshProducts[index];
        if (!runtimeProductRegistry) {
            continue;
        }

        const RemeshProduct* remeshProduct = runtimeProductRegistry->resolveRemesh(remeshProductHandle);
        if (!remeshProduct || !remeshProduct->isValid()) {
            continue;
        }

        const uint32_t runtimeModelId = remeshProduct->runtimeModelId;
        if (runtimeModelId == 0) {
            continue;
        }
        if (!seenRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }

        NodeGraphNodeId receiverModelNode{};
        const uint32_t receiverModelNodeId =
            topology.findFirstUpstreamNodeByType(remeshProductHandle.outputSocketKey, nodegraphtypes::Model, receiverModelNode)
            ? receiverModelNode.value
            : 0u;
        const auto explicitIt = presetByNodeModelId.find(receiverModelNodeId);
        if (explicitIt == presetByNodeModelId.end()) {
            continue;
        }

        const HeatMaterialPresetId presetId = explicitIt->second;
        const HeatMaterialPreset& preset = heatMaterialPresetById(presetId);
        RuntimeThermalMaterial runtimeMaterial{};
        runtimeMaterial.runtimeModelId = runtimeModelId;
        runtimeMaterial.density = preset.density;
        runtimeMaterial.specificHeat = preset.specificHeat;
        runtimeMaterial.conductivity = preset.conductivity;
        runtimeThermalMaterials.push_back(runtimeMaterial);
    }

    std::sort(
        runtimeThermalMaterials.begin(),
        runtimeThermalMaterials.end(),
        [](const RuntimeThermalMaterial& lhs, const RuntimeThermalMaterial& rhs) {
            return lhs.runtimeModelId < rhs.runtimeModelId;
        });

    return runtimeThermalMaterials;
}
