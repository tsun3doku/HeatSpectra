#include "RuntimePackageCompiler.hpp"

#include "nodegraph/NodeGraphRuntimeBridge.hpp"
#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeGraphHash.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "scene/SceneController.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

static uint64_t buildGeometryPackageHash(const GeometryPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 1u);
    NodeGraphHash::combine(hash, package.geometry.payloadHash);
    return hash;
}

static uint64_t buildRemeshPackageHash(const RemeshPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 2u);
    NodeGraphHash::combine(hash, package.sourceGeometry.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.params.iterations));
    NodeGraphHash::combineFloat(hash, package.params.minAngleDegrees);
    NodeGraphHash::combineFloat(hash, package.params.maxEdgeLength);
    NodeGraphHash::combineFloat(hash, package.params.stepSize);
    return hash;
}

static uint64_t buildVoronoiPackageHash(const VoronoiPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 3u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverModelProducts.size()));
    for (const ProductHandle& handle : package.receiverModelProducts) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(handle.type));
        NodeGraphHash::combine(hash, handle.outputSocketKey);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(handle.type));
        NodeGraphHash::combine(hash, handle.outputSocketKey);
    }
    return hash;
}

static uint64_t buildContactPackageHash(const ContactPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 4u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.emitterRemeshProduct.type));
    NodeGraphHash::combine(hash, package.emitterRemeshProduct.outputSocketKey);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProduct.type));
    NodeGraphHash::combine(hash, package.receiverRemeshProduct.outputSocketKey);
    return hash;
}

static uint64_t buildHeatPackageHash(const HeatPackage& package) {
    uint64_t hash = NodeGraphHash::start();
    NodeGraphHash::combine(hash, 5u);
    NodeGraphHash::combine(hash, package.authored.payloadHash);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.voronoiProduct.type));
    NodeGraphHash::combine(hash, package.voronoiProduct.outputSocketKey);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.contactProduct.type));
    NodeGraphHash::combine(hash, package.contactProduct.outputSocketKey);
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.sourceRemeshProducts.size()));
    for (const ProductHandle& handle : package.sourceRemeshProducts) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(handle.type));
        NodeGraphHash::combine(hash, handle.outputSocketKey);
    }
    NodeGraphHash::combine(hash, static_cast<uint64_t>(package.receiverRemeshProducts.size()));
    for (const ProductHandle& handle : package.receiverRemeshProducts) {
        NodeGraphHash::combine(hash, static_cast<uint64_t>(handle.type));
        NodeGraphHash::combine(hash, handle.outputSocketKey);
    }
    return hash;
}

namespace {

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
    if (!runtimeProductRegistry || meshHandle.key == 0) {
        return {};
    }

    const ProductHandle remeshHandle = (runtimeBridge && payloadRegistry)
        ? runtimeBridge->resolveRemeshProductForPayload(meshHandle)
        : ProductHandle{};
    if (remeshHandle.isValid()) {
        return runtimeProductRegistry->getPublishedHandle(NodeProductType::Model, remeshHandle.outputSocketKey);
    }

    return runtimeProductRegistry->getPublishedHandle(NodeProductType::Model, meshHandle.key);
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

}

void RuntimePackageCompiler::setSceneController(SceneController* updatedSceneController) {
    sceneController = updatedSceneController;
}

void RuntimePackageCompiler::setRuntimeBridge(const NodeGraphRuntimeBridge* updatedRuntimeBridge) {
    runtimeBridge = updatedRuntimeBridge;
}

void RuntimePackageCompiler::setRuntimeProductRegistry(const RuntimeProductRegistry* updatedRuntimeProductRegistry) {
    runtimeProductRegistry = updatedRuntimeProductRegistry;
}

GeometryPackage RuntimePackageCompiler::buildGeometryPackage(uint64_t socketKey, const GeometryData& geometry) const {
    GeometryPackage package{};
    package.geometry = geometry;
    if (socketKey != 0) {
        package.modelProduct.type = NodeProductType::Model;
        package.modelProduct.outputSocketKey = socketKey;
    }
    package.packageHash = buildGeometryPackageHash(package);
    return package;
}

RemeshPackage RuntimePackageCompiler::buildRemeshPackage(
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
    package.remeshHandle = remeshHandle;
    package.packageHash = buildRemeshPackageHash(package);
    return package;
}

VoronoiPackage RuntimePackageCompiler::buildVoronoiPackage(const NodePayloadRegistry* payloadRegistry, const VoronoiData& voronoi) const {
    VoronoiPackage package{};
    package.authored = voronoi;

    if (!payloadRegistry || !voronoi.active || voronoi.receiverMeshHandles.empty()) {
        return package;
    }

    package.receiverModelProducts.reserve(voronoi.receiverMeshHandles.size());
    package.receiverRemeshProducts.reserve(voronoi.receiverMeshHandles.size());

    std::set<NodeDataHandle> seenMeshHandles;
    for (const NodeDataHandle& meshHandle : voronoi.receiverMeshHandles) {
        if (!seenMeshHandles.insert(meshHandle).second) {
            continue;
        }

        const ProductHandle receiverRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverModelProducts.push_back(modelProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle));
        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    package.packageHash = buildVoronoiPackageHash(package);
    return package;
}

HeatPackage RuntimePackageCompiler::buildHeatPackage(
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

        const ProductHandle sourceRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, heatSource->meshHandle);
        if (!sourceRemeshProduct.isValid()) {
            continue;
        }
        if (!seenSourceRemeshSocketKeys.insert(sourceRemeshProduct.outputSocketKey).second) {
            continue;
        }

        package.sourceRemeshProducts.push_back(sourceRemeshProduct);
        package.sourceTemperatures.push_back(heatSource->temperature);
    }

    std::set<NodeDataHandle> seenReceiverHandles;
    package.receiverRemeshProducts.reserve(heat.receiverMeshHandles.size());
    for (const NodeDataHandle& meshHandle : heat.receiverMeshHandles) {
        if (!seenReceiverHandles.insert(meshHandle).second) {
            continue;
        }

        const ProductHandle receiverRemeshProduct =
            remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, meshHandle);
        if (!receiverRemeshProduct.isValid()) {
            continue;
        }

        package.receiverRemeshProducts.push_back(receiverRemeshProduct);
    }

    package.runtimeThermalMaterials = buildRuntimeThermalMaterials(
        package.receiverRemeshProducts,
        heat.materialBindings);

    package.packageHash = buildHeatPackageHash(package);
    return package;
}

ContactPackage RuntimePackageCompiler::buildContactPackage(const NodePayloadRegistry* payloadRegistry, const ContactData& contact) const {
    ContactPackage package{};
    package.authored = contact;
    if (!payloadRegistry || !contact.active || !contact.pair.hasValidContact) {
        return package;
    }

    const ContactPairData& pair = contact.pair;
    if (!pair.hasValidContact ||
        pair.endpointA.meshHandle.key == 0 ||
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

    package.emitterRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, emitterEndpoint.meshHandle);
    package.receiverRemeshProduct = remeshProductFromHandle(runtimeBridge, payloadRegistry, runtimeProductRegistry, receiverEndpoint.meshHandle);

    package.packageHash = buildContactPackageHash(package);
    return package;
}

RuntimePackageSet RuntimePackageCompiler::buildRuntimePackageSet(
    const NodeGraphState& graphState,
    const NodeGraphEvaluationState& evaluationState,
    const NodePayloadRegistry* payloadRegistry) const {
    RuntimePackageSet packageSet{};

    for (const NodeGraphNode& node : graphState.nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedPayloadType == NodePayloadType::None) {
                continue;
            }

            const uint64_t outputSocketKey = socketKey(node.id, output.id);
            const auto valueIt = evaluationState.outputBySocket.find(outputSocketKey);
            if (valueIt == evaluationState.outputBySocket.end() ||
                valueIt->second.status != EvaluatedSocketStatus::Value) {
                continue;
            }

            const NodeDataBlock& outputBlock = valueIt->second.data;
            if (outputBlock.payloadHandle.key == 0) {
                continue;
            }

            switch (output.contract.producedPayloadType) {
            case NodePayloadType::Geometry: {
                const GeometryData* geometry = payloadRegistry
                    ? payloadRegistry->get<GeometryData>(outputBlock.payloadHandle)
                    : nullptr;
                if (!geometry || geometry->modelId == 0) {
                    break;
                }
                packageSet.geometryBySocket.emplace(outputSocketKey, buildGeometryPackage(outputSocketKey, *geometry));
                break;
            }
            case NodePayloadType::Remesh: {
                const RemeshData* remesh = payloadRegistry
                    ? payloadRegistry->get<RemeshData>(outputBlock.payloadHandle)
                    : nullptr;
                if (!remesh || !remesh->active || remesh->sourceMeshHandle.key == 0) {
                    break;
                }
                packageSet.remeshBySocket.emplace(
                    outputSocketKey,
                    buildRemeshPackage(*remesh, payloadRegistry, outputBlock.payloadHandle));
                break;
            }
            case NodePayloadType::Voronoi: {
                const VoronoiData* voronoi = payloadRegistry
                    ? payloadRegistry->get<VoronoiData>(outputBlock.payloadHandle)
                    : nullptr;
                if (!voronoi) {
                    break;
                }
                packageSet.voronoiBySocket.emplace(
                    outputSocketKey,
                    buildVoronoiPackage(payloadRegistry, *voronoi));
                break;
            }
            case NodePayloadType::Contact:
            case NodePayloadType::Heat:
                break;
            default:
                break;
            }
        }
    }

    for (const NodeGraphNode& node : graphState.nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedPayloadType != NodePayloadType::Contact) {
                continue;
            }

            const uint64_t outputSocketKey = socketKey(node.id, output.id);
            const auto valueIt = evaluationState.outputBySocket.find(outputSocketKey);
            if (valueIt == evaluationState.outputBySocket.end() ||
                valueIt->second.status != EvaluatedSocketStatus::Value) {
                continue;
            }

            const NodeDataBlock& outputBlock = valueIt->second.data;
            if (outputBlock.payloadHandle.key == 0) {
                continue;
            }

            const ContactData* contact = payloadRegistry
                ? payloadRegistry->get<ContactData>(outputBlock.payloadHandle)
                : nullptr;
            if (!contact) {
                continue;
            }

            packageSet.contactBySocket.emplace(
                outputSocketKey,
                buildContactPackage(payloadRegistry, *contact));
        }
    }

    for (const NodeGraphNode& node : graphState.nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedPayloadType != NodePayloadType::Heat) {
                continue;
            }

            const uint64_t outputSocketKey = socketKey(node.id, output.id);
            const auto valueIt = evaluationState.outputBySocket.find(outputSocketKey);
            if (valueIt == evaluationState.outputBySocket.end() ||
                valueIt->second.status != EvaluatedSocketStatus::Value) {
                continue;
            }

            const NodeDataBlock& outputBlock = valueIt->second.data;
            if (outputBlock.payloadHandle.key == 0) {
                continue;
            }

            const HeatData* heat = payloadRegistry
                ? payloadRegistry->get<HeatData>(outputBlock.payloadHandle)
                : nullptr;
            if (!heat) {
                continue;
            }

            const ProductHandle voronoiProduct = resolveHeatVoronoiInput(node, evaluationState, runtimeProductRegistry);
            const ProductHandle contactProduct = resolveHeatContactInput(node, evaluationState, runtimeProductRegistry);
            packageSet.heatBySocket.emplace(
                outputSocketKey,
                buildHeatPackage(payloadRegistry, *heat, voronoiProduct, contactProduct));
        }
    }

    return packageSet;
}

bool RuntimePackageCompiler::tryParseHeatMaterialModelId(const std::string& value, uint32_t& outNodeModelId) const {
    outNodeModelId = 0;
    if (value.empty()) {
        return false;
    }

    auto isUnsignedInteger = [](const std::string& token) {
        if (token.empty()) {
            return false;
        }

        for (char character : token) {
            if (character < '0' || character > '9') {
                return false;
            }
        }
        return true;
    };

    std::string token = value;
    constexpr const char* receiverPrefix = "receiver:";
    constexpr const char* modelPrefix = "model:";
    if (token.rfind(receiverPrefix, 0) == 0) {
        token = token.substr(9);
    } else if (token.rfind(modelPrefix, 0) == 0) {
        token = token.substr(6);
    }

    if (!isUnsignedInteger(token)) {
        return false;
    }

    try {
        const unsigned long parsed = std::stoul(token);
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        outNodeModelId = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        outNodeModelId = 0;
        return false;
    }
}

std::vector<RuntimeThermalMaterial> RuntimePackageCompiler::buildRuntimeThermalMaterials(
    const std::vector<ProductHandle>& receiverRemeshProducts,
    const std::vector<HeatMaterialBindingEntry>& materialBindings) const {
    std::unordered_map<uint32_t, HeatMaterialPresetId> presetByNodeModelId;
    std::optional<HeatMaterialPresetId> fallbackPreset;
    for (const HeatMaterialBindingEntry& binding : materialBindings) {
        uint32_t nodeModelId = 0;
        if (tryParseHeatMaterialModelId(binding.groupName, nodeModelId) && nodeModelId != 0) {
            presetByNodeModelId[nodeModelId] = binding.presetId;
        } else if (!fallbackPreset.has_value()) {
            fallbackPreset = binding.presetId;
        }
    }

    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
    runtimeThermalMaterials.reserve(receiverRemeshProducts.size());
    std::unordered_set<uint32_t> seenRuntimeModelIds;
    for (const ProductHandle& remeshProductHandle : receiverRemeshProducts) {
        if (!runtimeProductRegistry) {
            continue;
        }

        const RemeshProduct* remeshProduct = runtimeProductRegistry->resolveRemesh(remeshProductHandle);
        if (!remeshProduct || !remeshProduct->isValid()) {
            continue;
        }

        const GeometryData& geometry = remeshProduct->geometry;
        const uint32_t runtimeModelId = remeshProduct->runtimeModelId;
        if (geometry.modelId == 0 || runtimeModelId == 0) {
            continue;
        }
        if (!seenRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        const auto explicitIt = presetByNodeModelId.find(geometry.modelId);
        if (explicitIt != presetByNodeModelId.end()) {
            presetId = explicitIt->second;
        } else if (fallbackPreset.has_value()) {
            presetId = *fallbackPreset;
        }

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
