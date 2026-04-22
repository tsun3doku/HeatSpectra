#pragma once

#include <algorithm>
#include <unordered_set>

#include "contact/ContactTypes.hpp"
#include "heat/HeatSystemComputeController.hpp"
#include "heat/HeatSystemPresets.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

class RuntimeHeatComputeTransport {
public:
    void setController(HeatSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void sync(const ECSRegistry& registry) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;

        auto view = registry.view<HeatPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            const auto& package = registry.get<HeatPackage>(entity);
            if (!package.authored.active) {
                continue;
            }
            if (package.sourceTemperatures.size() < package.sourceRemeshProducts.size()) {
                continue;
            }

            auto hashIt = appliedPackageHash.find(socketKey);
            if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
                nextSocketKeys.insert(socketKey);
                continue;
            }

            HeatSystemComputeController::Config config{};
            config.active = package.authored.active;
            config.paused = package.authored.paused;
            config.resetRequested = package.authored.resetRequested;
            config.sourceIntrinsicMeshes.reserve(package.sourceRemeshProducts.size());
            config.sourceRuntimeModelIds.reserve(package.sourceRemeshProducts.size());
            config.sourceTemperatureByRuntimeId.clear();
            size_t sourceIndex = 0;
            for (; sourceIndex < package.sourceRemeshProducts.size(); ++sourceIndex) {
                const ProductHandle& remeshProductHandle = package.sourceRemeshProducts[sourceIndex];
                const RemeshProduct* product =
                    tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
                if (!product || product->runtimeModelId == 0) {
                    break;
                }

                config.sourceIntrinsicMeshes.push_back(product->intrinsicMesh);
                config.sourceRuntimeModelIds.push_back(product->runtimeModelId);
                config.sourceTemperatureByRuntimeId[product->runtimeModelId] = package.sourceTemperatures[sourceIndex];
            }
            if (sourceIndex != package.sourceRemeshProducts.size()) {
                continue;
            }

            const size_t receiverCount = package.receiverRemeshProducts.size();
            config.receiverIntrinsicMeshes.resize(receiverCount);
            config.receiverRuntimeModelIds.resize(receiverCount, 0);
            config.supportingHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
            config.supportingAngleViews.resize(receiverCount, VK_NULL_HANDLE);
            config.halfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
            config.edgeViews.resize(receiverCount, VK_NULL_HANDLE);
            config.triangleViews.resize(receiverCount, VK_NULL_HANDLE);
            config.lengthViews.resize(receiverCount, VK_NULL_HANDLE);
            config.inputHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
            config.inputEdgeViews.resize(receiverCount, VK_NULL_HANDLE);
            config.inputTriangleViews.resize(receiverCount, VK_NULL_HANDLE);
            config.inputLengthViews.resize(receiverCount, VK_NULL_HANDLE);
            size_t receiverIndex = 0;
            for (; receiverIndex < receiverCount; ++receiverIndex) {
                const ProductHandle& remeshProductHandle = package.receiverRemeshProducts[receiverIndex];
                const RemeshProduct* product =
                    tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
                if (!product || product->runtimeModelId == 0) {
                    break;
                }

                config.receiverIntrinsicMeshes[receiverIndex] = product->intrinsicMesh;
                config.receiverRuntimeModelIds[receiverIndex] = product->runtimeModelId;
                config.supportingHalfedgeViews[receiverIndex] = product->supportingHalfedgeView;
                config.supportingAngleViews[receiverIndex] = product->supportingAngleView;
                config.halfedgeViews[receiverIndex] = product->halfedgeView;
                config.edgeViews[receiverIndex] = product->edgeView;
                config.triangleViews[receiverIndex] = product->triangleView;
                config.lengthViews[receiverIndex] = product->lengthView;
                config.inputHalfedgeViews[receiverIndex] = product->inputHalfedgeView;
                config.inputEdgeViews[receiverIndex] = product->inputEdgeView;
                config.inputTriangleViews[receiverIndex] = product->inputTriangleView;
                config.inputLengthViews[receiverIndex] = product->inputLengthView;
            }
            if (receiverIndex != receiverCount) {
                continue;
            }

            std::unordered_map<uint32_t, HeatMaterialPresetId> presetByNodeModelId;
            for (const HeatMaterialBinding& binding : package.authored.materialBindings) {
                if (binding.receiverModelNodeId != 0) {
                    presetByNodeModelId[binding.receiverModelNodeId] = binding.presetId;
                }
            }

            config.runtimeThermalMaterials.reserve(receiverCount);
            std::unordered_set<uint32_t> seenRuntimeModelIds;
            for (size_t receiverIndex = 0; receiverIndex < receiverCount; ++receiverIndex) {
                const uint32_t runtimeModelId = config.receiverRuntimeModelIds[receiverIndex];
                if (runtimeModelId == 0) {
                    continue;
                }
                if (!seenRuntimeModelIds.insert(runtimeModelId).second) {
                    continue;
                }

                const uint32_t receiverModelNodeId = static_cast<uint32_t>(package.receiverModelProducts[receiverIndex].outputSocketKey >> 32);
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
                config.runtimeThermalMaterials.push_back(runtimeMaterial);
            }

            std::sort(
                config.runtimeThermalMaterials.begin(),
                config.runtimeThermalMaterials.end(),
                [](const RuntimeThermalMaterial& lhs, const RuntimeThermalMaterial& rhs) {
                    return lhs.runtimeModelId < rhs.runtimeModelId;
                });

            if (package.voronoiProduct.isValid()) {
                const VoronoiProduct* product = tryGetProduct<VoronoiProduct>(*ecsRegistry, package.voronoiProduct.outputSocketKey);
                if (!product) {
                    continue;
                }

                config.voronoiNodeCount = product->nodeCount;
                config.voronoiNodes = product->mappedVoronoiNodes;
                config.voronoiNodeBuffer = product->nodeBuffer;
                config.voronoiNodeBufferOffset = product->nodeBufferOffset;
                config.voronoiNeighborBuffer = product->voronoiNeighborBuffer;
                config.voronoiNeighborBufferOffset = product->voronoiNeighborBufferOffset;
                config.neighborIndicesBuffer = product->neighborIndicesBuffer;
                config.neighborIndicesBufferOffset = product->neighborIndicesBufferOffset;
                config.interfaceAreasBuffer = product->interfaceAreasBuffer;
                config.interfaceAreasBufferOffset = product->interfaceAreasBufferOffset;
                config.interfaceNeighborIdsBuffer = product->interfaceNeighborIdsBuffer;
                config.interfaceNeighborIdsBufferOffset = product->interfaceNeighborIdsBufferOffset;
                config.seedFlagsBuffer = product->seedFlagsBuffer;
                config.seedFlagsBufferOffset = product->seedFlagsBufferOffset;

                for (const VoronoiSurfaceProduct& surfaceProduct : product->surfaces) {
                    const uint32_t runtimeModelId = surfaceProduct.runtimeModelId;
                    if (runtimeModelId == 0) {
                        continue;
                    }

                    config.receiverVoronoiNodeOffsetByModelId[runtimeModelId] = surfaceProduct.nodeOffset;
                    config.receiverVoronoiNodeCountByModelId[runtimeModelId] = surfaceProduct.nodeCount;
                    config.receiverVoronoiSurfaceMappingBufferByModelId[runtimeModelId] = surfaceProduct.surfaceMappingBuffer;
                    config.receiverVoronoiSurfaceMappingBufferOffsetByModelId[runtimeModelId] = surfaceProduct.surfaceMappingBufferOffset;
                    config.receiverVoronoiSurfaceCellIndicesByModelId[runtimeModelId] = surfaceProduct.surfaceCellIndices;
                    config.receiverVoronoiSeedFlagsByModelId[runtimeModelId] = surfaceProduct.seedFlags;
                }
            }

            if (package.contactProduct.isValid()) {
                const ContactProduct* product = tryGetProduct<ContactProduct>(*ecsRegistry, package.contactProduct.outputSocketKey);
                if (!product) {
                    continue;
                }

                config.contactCouplings = { product->coupling };
            }

            config.computeHash = buildComputeHash(config);
            controller->configure(socketKey, config);
            nextSocketKeys.insert(socketKey);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                controller->disable(socketKey);
                appliedPackageHash.erase(socketKey);
            }
        }

        activeSocketKeys = std::move(nextSocketKeys);
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        std::vector<uint64_t> removals;
        auto productView = ecsRegistry->view<HeatProduct>();
        for (auto entity : productView) {
            const uint64_t socketKey = static_cast<uint64_t>(entity);
            if (activeSocketKeys.find(socketKey) == activeSocketKeys.end()) {
                removals.push_back(socketKey);
            }
        }

        for (uint64_t socketKey : removals) {
            auto entity = static_cast<ECSEntity>(socketKey);
            ecsRegistry->remove<HeatProduct>(entity);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            auto entity = static_cast<ECSEntity>(socketKey);
            const auto& package = ecsRegistry->get<HeatPackage>(entity);
            auto hashIt = appliedPackageHash.find(socketKey);
            const HeatProduct* product = tryGetProduct<HeatProduct>(*ecsRegistry, socketKey);
            if (!product || hashIt == appliedPackageHash.end() || hashIt->second != package.packageHash) {
                publishProduct(socketKey);
            }
        }
    }

private:
    void publishProduct(uint64_t socketKey) {
        if (!controller || socketKey == 0) {
            return;
        }

        HeatProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            auto entity = static_cast<ECSEntity>(socketKey);
            ecsRegistry->remove<HeatProduct>(entity);
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        const auto& package = ecsRegistry->get<HeatPackage>(entity);
        ecsRegistry->emplace_or_replace<HeatProduct>(entity, product);
        appliedPackageHash[socketKey] = package.packageHash;
    }

    HeatSystemComputeController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_map<uint64_t, uint64_t> appliedPackageHash;
};
