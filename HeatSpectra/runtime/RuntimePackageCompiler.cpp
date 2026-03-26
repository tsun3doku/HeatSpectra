#include "RuntimePackageCompiler.hpp"

#include "nodegraph/NodePayloadRegistry.hpp"
#include "scene/SceneController.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

void RuntimePackageCompiler::setSceneController(SceneController* updatedSceneController) {
    sceneController = updatedSceneController;
}

bool RuntimePackageCompiler::resolveRuntimeModel(const GeometryData& geometry, uint32_t& outRuntimeModelId) const {
    outRuntimeModelId = 0;
    if (geometry.modelId == 0 || !sceneController) {
        return false;
    }

    uint32_t runtimeModelId = 0;
    if (!sceneController->tryGetNodeModelRuntimeId(geometry.modelId, runtimeModelId) ||
        runtimeModelId == 0) {
        return false;
    }

    outRuntimeModelId = runtimeModelId;
    return true;
}

GeometryPackage RuntimePackageCompiler::buildGeometryPackage(const GeometryData& geometry) const {
    GeometryPackage package{};
    package.geometry = geometry;
    resolveRuntimeModel(geometry, package.runtimeModelId);
    return package;
}

VoronoiPackage RuntimePackageCompiler::buildVoronoiPackage(const NodePayloadRegistry* payloadRegistry, const VoronoiData& voronoi) const {
    VoronoiPackage package{};
    package.authored = voronoi;

    if (!payloadRegistry || !voronoi.active || voronoi.receiverGeometryHandles.empty()) {
        return package;
    }

    package.receiverGeometryHandles.reserve(voronoi.receiverGeometryHandles.size());
    package.receiverGeometries.reserve(voronoi.receiverGeometryHandles.size());
    package.receiverIntrinsics.reserve(voronoi.receiverGeometryHandles.size());
    package.receiverRuntimeModelIds.reserve(voronoi.receiverGeometryHandles.size());

    std::set<NodeDataHandle> seenGeometryHandles;
    for (const NodeDataHandle& geometryHandle : voronoi.receiverGeometryHandles) {
        if (!seenGeometryHandles.insert(geometryHandle).second) {
            continue;
        }

        const GeometryData* geometry = payloadRegistry->get<GeometryData>(geometryHandle);
        if (!geometry || geometry->intrinsicHandle.key == 0) {
            continue;
        }

        const IntrinsicMeshData* intrinsic = payloadRegistry->get<IntrinsicMeshData>(geometry->intrinsicHandle);
        if (!intrinsic) {
            continue;
        }

        uint32_t runtimeModelId = 0;
        if (!resolveRuntimeModel(*geometry, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        package.receiverGeometryHandles.push_back(geometryHandle);
        package.receiverGeometries.push_back(*geometry);
        package.receiverIntrinsics.push_back(*intrinsic);
        package.receiverRuntimeModelIds.push_back(runtimeModelId);
    }

    return package;
}

HeatPackage RuntimePackageCompiler::buildHeatPackage(
    const NodePayloadRegistry* payloadRegistry,
    const HeatData& heat,
    const VoronoiData* voronoi) const {
    HeatPackage package{};
    package.authored = heat;
    if (voronoi) {
        package.voronoiActive = voronoi->active;
        package.voronoiParams = voronoi->params;
    }

    if (!payloadRegistry) {
        return package;
    }

    std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
    package.sourceGeometries.reserve(heat.sourceHandles.size());
    package.sourceIntrinsics.reserve(heat.sourceHandles.size());
    package.sourceRuntimeModelIds.reserve(heat.sourceHandles.size());
    for (const NodeDataHandle& sourceHandle : heat.sourceHandles) {
        if (sourceHandle.key == 0) {
            continue;
        }

        const HeatSourceData* heatSource = payloadRegistry->get<HeatSourceData>(sourceHandle);
        if (!heatSource || heatSource->geometryHandle.key == 0) {
            continue;
        }

        const GeometryData* sourceGeometry = payloadRegistry->get<GeometryData>(heatSource->geometryHandle);
        if (!sourceGeometry || sourceGeometry->intrinsicHandle.key == 0) {
            continue;
        }

        const IntrinsicMeshData* sourceIntrinsic = payloadRegistry->get<IntrinsicMeshData>(sourceGeometry->intrinsicHandle);
        if (!sourceIntrinsic) {
            continue;
        }

        uint32_t runtimeModelId = 0;
        if (!resolveRuntimeModel(*sourceGeometry, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }
        if (!seenSourceRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }

        package.sourceGeometries.push_back(*sourceGeometry);
        package.sourceIntrinsics.push_back(*sourceIntrinsic);
        package.sourceRuntimeModelIds.push_back(runtimeModelId);
        package.sourceTemperatureByRuntimeId.emplace(runtimeModelId, heatSource->temperature);
    }

    std::set<NodeDataHandle> seenReceiverHandles;
    package.receiverGeometryHandles.reserve(heat.receiverGeometryHandles.size());
    package.receiverGeometries.reserve(heat.receiverGeometryHandles.size());
    package.receiverIntrinsics.reserve(heat.receiverGeometryHandles.size());
    package.receiverRuntimeModelIds.reserve(heat.receiverGeometryHandles.size());
    for (const NodeDataHandle& geometryHandle : heat.receiverGeometryHandles) {
        const GeometryData* geometry = payloadRegistry->get<GeometryData>(geometryHandle);
        if (!geometry || geometry->intrinsicHandle.key == 0) {
            continue;
        }
        if (!seenReceiverHandles.insert(geometryHandle).second) {
            continue;
        }

        const IntrinsicMeshData* intrinsic = payloadRegistry->get<IntrinsicMeshData>(geometry->intrinsicHandle);
        if (!intrinsic) {
            continue;
        }

        uint32_t runtimeModelId = 0;
        if (!resolveRuntimeModel(*geometry, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        package.receiverGeometryHandles.push_back(geometryHandle);
        package.receiverGeometries.push_back(*geometry);
        package.receiverIntrinsics.push_back(*intrinsic);
        package.receiverRuntimeModelIds.push_back(runtimeModelId);
    }

    package.runtimeThermalMaterials = buildRuntimeThermalMaterials(
        package.receiverGeometries,
        package.receiverRuntimeModelIds,
        heat.materialBindings);

    return package;
}

ContactPackage RuntimePackageCompiler::buildContactPackage(
    const NodePayloadRegistry* payloadRegistry,
    const ContactData& contact) const {
    ContactPackage package{};
    package.authored = contact;
    if (!payloadRegistry || !contact.active || contact.bindings.empty()) {
        return package;
    }

    package.runtimeContactPairs.reserve(contact.bindings.size());
    for (const ContactBindingData& input : contact.bindings) {
        const ContactPairData& pair = input.pair;
        if (!pair.hasValidContact ||
            pair.endpointA.geometryHandle.key == 0 ||
            pair.endpointB.geometryHandle.key == 0 ||
            pair.endpointA.geometryHandle == pair.endpointB.geometryHandle) {
            continue;
        }

        const ContactPairEndpoint& emitterEndpoint =
            (pair.endpointA.role == ContactPairRole::Source)
            ? pair.endpointA
            : pair.endpointB;
        const ContactPairEndpoint& receiverEndpoint =
            (pair.endpointA.role == ContactPairRole::Source)
            ? pair.endpointB
            : pair.endpointA;

        const GeometryData* emitterGeometry = payloadRegistry->get<GeometryData>(emitterEndpoint.geometryHandle);
        const GeometryData* receiverGeometry = payloadRegistry->get<GeometryData>(receiverEndpoint.geometryHandle);
        if (!emitterGeometry ||
            !receiverGeometry ||
            emitterGeometry->intrinsicHandle.key == 0 ||
            receiverGeometry->intrinsicHandle.key == 0) {
            continue;
        }

        const IntrinsicMeshData* emitterIntrinsic = payloadRegistry->get<IntrinsicMeshData>(emitterGeometry->intrinsicHandle);
        const IntrinsicMeshData* receiverIntrinsic = payloadRegistry->get<IntrinsicMeshData>(receiverGeometry->intrinsicHandle);
        if (!emitterIntrinsic || !receiverIntrinsic) {
            continue;
        }

        uint32_t emitterRuntimeModelId = 0;
        uint32_t receiverRuntimeModelId = 0;
        if (!resolveRuntimeModel(*emitterGeometry, emitterRuntimeModelId) ||
            !resolveRuntimeModel(*receiverGeometry, receiverRuntimeModelId) ||
            emitterRuntimeModelId == 0 ||
            receiverRuntimeModelId == 0 ||
            emitterRuntimeModelId == receiverRuntimeModelId) {
            continue;
        }

        RuntimeContactBinding runtimeContactPair{};
        runtimeContactPair.contactPair = pair;
        runtimeContactPair.payloadPair.couplingType = pair.kind;
        runtimeContactPair.payloadPair.minNormalDot = pair.minNormalDot;
        runtimeContactPair.payloadPair.contactRadius = pair.contactRadius;
        runtimeContactPair.payloadPair.emitter.geometryHandle = emitterEndpoint.geometryHandle;
        runtimeContactPair.payloadPair.emitter.geometry = *emitterGeometry;
        runtimeContactPair.payloadPair.emitter.intrinsic = *emitterIntrinsic;
        runtimeContactPair.payloadPair.receiver.geometryHandle = receiverEndpoint.geometryHandle;
        runtimeContactPair.payloadPair.receiver.geometry = *receiverGeometry;
        runtimeContactPair.payloadPair.receiver.intrinsic = *receiverIntrinsic;
        runtimeContactPair.emitterRuntimeModelId = emitterRuntimeModelId;
        runtimeContactPair.receiverRuntimeModelId = receiverRuntimeModelId;
        package.runtimeContactPairs.push_back(runtimeContactPair);
    }

    std::sort(
        package.runtimeContactPairs.begin(),
        package.runtimeContactPairs.end(),
        [](const RuntimeContactBinding& lhs, const RuntimeContactBinding& rhs) {
            return std::tie(
                       lhs.contactPair.kind,
                       lhs.contactPair.endpointA.geometryHandle.key,
                       lhs.contactPair.endpointA.geometryHandle.revision,
                       lhs.contactPair.endpointA.geometryHandle.count,
                       lhs.contactPair.endpointB.geometryHandle.key,
                       lhs.contactPair.endpointB.geometryHandle.revision,
                       lhs.contactPair.endpointB.geometryHandle.count,
                       lhs.emitterRuntimeModelId,
                       lhs.receiverRuntimeModelId,
                       lhs.contactPair.minNormalDot,
                       lhs.contactPair.contactRadius) <
                std::tie(
                       rhs.contactPair.kind,
                       rhs.contactPair.endpointA.geometryHandle.key,
                       rhs.contactPair.endpointA.geometryHandle.revision,
                       rhs.contactPair.endpointA.geometryHandle.count,
                       rhs.contactPair.endpointB.geometryHandle.key,
                       rhs.contactPair.endpointB.geometryHandle.revision,
                       rhs.contactPair.endpointB.geometryHandle.count,
                       rhs.emitterRuntimeModelId,
                       rhs.receiverRuntimeModelId,
                       rhs.contactPair.minNormalDot,
                       rhs.contactPair.contactRadius);
        });

    std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
    package.sourceRuntimeModelIds.reserve(package.runtimeContactPairs.size());
    package.sourceGeometries.reserve(package.runtimeContactPairs.size());
    package.sourceIntrinsics.reserve(package.runtimeContactPairs.size());
    for (const RuntimeContactBinding& contactPair : package.runtimeContactPairs) {
        const uint32_t runtimeModelId = contactPair.emitterRuntimeModelId;
        if (runtimeModelId == 0 || !seenSourceRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }

        package.sourceRuntimeModelIds.push_back(runtimeModelId);
        package.sourceGeometries.push_back(contactPair.payloadPair.emitter.geometry);
        package.sourceIntrinsics.push_back(contactPair.payloadPair.emitter.intrinsic);
    }

    return package;
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
    const std::vector<GeometryData>& receiverGeometries,
    const std::vector<uint32_t>& receiverRuntimeModelIds,
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
    runtimeThermalMaterials.reserve(receiverRuntimeModelIds.size());
    std::unordered_set<uint32_t> seenRuntimeModelIds;
    for (size_t index = 0; index < receiverGeometries.size(); ++index) {
        const GeometryData& geometry = receiverGeometries[index];
        const uint32_t runtimeModelId = receiverRuntimeModelIds[index];
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
