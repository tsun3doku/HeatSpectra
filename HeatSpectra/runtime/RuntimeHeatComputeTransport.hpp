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
            auto hashIt = appliedPackageHash.find(socketKey);
            if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
                nextSocketKeys.insert(socketKey);
                continue;
            }

            HeatSystemComputeController::Config config{};
            if (!tryBuildConfig(socketKey, package, config)) {
                continue;
            }
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
            removePublishedProduct(socketKey);
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
    bool tryBuildConfig(uint64_t socketKey, const HeatPackage& package, HeatSystemComputeController::Config& outConfig) const {
        if (socketKey == 0 || !ecsRegistry) {
            return false;
        }
        if (!package.authored.active) {
            return false;
        }
        if (package.sourceTemperatures.size() < package.sourceRemeshProducts.size()) {
            return false;
        }
        if (package.receiverModelProducts.size() < package.receiverRemeshProducts.size()) {
            return false;
        }

        outConfig = {};
        outConfig.active = package.authored.active;
        outConfig.paused = package.authored.paused;
        outConfig.resetRequested = package.authored.resetRequested;
        outConfig.sourceIntrinsicMeshes.reserve(package.sourceRemeshProducts.size());
        outConfig.sourceRuntimeModelIds.reserve(package.sourceRemeshProducts.size());

        size_t sourceIndex = 0;
        for (; sourceIndex < package.sourceRemeshProducts.size(); ++sourceIndex) {
            const ProductHandle& remeshProductHandle = package.sourceRemeshProducts[sourceIndex];
            const RemeshProduct* product =
                tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
            if (!product || product->runtimeModelId == 0) {
                break;
            }

            outConfig.sourceIntrinsicMeshes.push_back(product->intrinsicMesh);
            outConfig.sourceRuntimeModelIds.push_back(product->runtimeModelId);
            outConfig.sourceTemperatureByRuntimeId[product->runtimeModelId] = package.sourceTemperatures[sourceIndex];
        }
        if (sourceIndex != package.sourceRemeshProducts.size()) {
            return false;
        }

        const size_t receiverCount = package.receiverRemeshProducts.size();
        outConfig.receiverIntrinsicMeshes.resize(receiverCount);
        outConfig.receiverRuntimeModelIds.resize(receiverCount, 0);
        outConfig.supportingHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.supportingAngleViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.halfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.edgeViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.triangleViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.lengthViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.inputHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.inputEdgeViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.inputTriangleViews.resize(receiverCount, VK_NULL_HANDLE);
        outConfig.inputLengthViews.resize(receiverCount, VK_NULL_HANDLE);

        size_t receiverIndex = 0;
        for (; receiverIndex < receiverCount; ++receiverIndex) {
            const ProductHandle& remeshProductHandle = package.receiverRemeshProducts[receiverIndex];
            const RemeshProduct* product =
                tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
            if (!product || product->runtimeModelId == 0) {
                break;
            }

            outConfig.receiverIntrinsicMeshes[receiverIndex] = product->intrinsicMesh;
            outConfig.receiverRuntimeModelIds[receiverIndex] = product->runtimeModelId;
            outConfig.supportingHalfedgeViews[receiverIndex] = product->supportingHalfedgeView;
            outConfig.supportingAngleViews[receiverIndex] = product->supportingAngleView;
            outConfig.halfedgeViews[receiverIndex] = product->halfedgeView;
            outConfig.edgeViews[receiverIndex] = product->edgeView;
            outConfig.triangleViews[receiverIndex] = product->triangleView;
            outConfig.lengthViews[receiverIndex] = product->lengthView;
            outConfig.inputHalfedgeViews[receiverIndex] = product->inputHalfedgeView;
            outConfig.inputEdgeViews[receiverIndex] = product->inputEdgeView;
            outConfig.inputTriangleViews[receiverIndex] = product->inputTriangleView;
            outConfig.inputLengthViews[receiverIndex] = product->inputLengthView;
        }
        if (receiverIndex != receiverCount) {
            return false;
        }

        std::unordered_map<uint32_t, HeatMaterialPresetId> presetByNodeModelId;
        for (const HeatMaterialBinding& binding : package.authored.materialBindings) {
            if (binding.receiverModelNodeId != 0) {
                presetByNodeModelId[binding.receiverModelNodeId] = binding.presetId;
            }
        }

        outConfig.runtimeThermalMaterials.reserve(receiverCount);
        std::unordered_set<uint32_t> seenRuntimeModelIds;
        for (size_t materialIndex = 0; materialIndex < receiverCount; ++materialIndex) {
            const uint32_t runtimeModelId = outConfig.receiverRuntimeModelIds[materialIndex];
            if (runtimeModelId == 0) {
                continue;
            }
            if (!seenRuntimeModelIds.insert(runtimeModelId).second) {
                continue;
            }

            const uint32_t receiverModelNodeId = static_cast<uint32_t>(package.receiverModelProducts[materialIndex].outputSocketKey >> 32);
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
            outConfig.runtimeThermalMaterials.push_back(runtimeMaterial);
        }

        std::sort(
            outConfig.runtimeThermalMaterials.begin(),
            outConfig.runtimeThermalMaterials.end(),
            [](const RuntimeThermalMaterial& lhs, const RuntimeThermalMaterial& rhs) {
                return lhs.runtimeModelId < rhs.runtimeModelId;
            });

        if (package.voronoiProduct.isValid()) {
            const VoronoiProduct* product = tryGetProduct<VoronoiProduct>(*ecsRegistry, package.voronoiProduct.outputSocketKey);
            if (!product) {
                return false;
            }

            outConfig.voronoiNodeCount = product->nodeCount;
            outConfig.voronoiNodes = product->mappedVoronoiNodes;
            outConfig.voronoiNodeBuffer = product->nodeBuffer;
            outConfig.voronoiNodeBufferOffset = product->nodeBufferOffset;
            outConfig.voronoiNeighborBuffer = product->voronoiNeighborBuffer;
            outConfig.voronoiNeighborBufferOffset = product->voronoiNeighborBufferOffset;
            outConfig.neighborIndicesBuffer = product->neighborIndicesBuffer;
            outConfig.neighborIndicesBufferOffset = product->neighborIndicesBufferOffset;
            outConfig.interfaceAreasBuffer = product->interfaceAreasBuffer;
            outConfig.interfaceAreasBufferOffset = product->interfaceAreasBufferOffset;
            outConfig.interfaceNeighborIdsBuffer = product->interfaceNeighborIdsBuffer;
            outConfig.interfaceNeighborIdsBufferOffset = product->interfaceNeighborIdsBufferOffset;
            outConfig.seedFlagsBuffer = product->seedFlagsBuffer;
            outConfig.seedFlagsBufferOffset = product->seedFlagsBufferOffset;

            for (const VoronoiSurfaceProduct& surfaceProduct : product->surfaces) {
                const uint32_t runtimeModelId = surfaceProduct.runtimeModelId;
                if (runtimeModelId == 0) {
                    continue;
                }

                outConfig.receiverVoronoiNodeOffsetByModelId[runtimeModelId] = surfaceProduct.nodeOffset;
                outConfig.receiverVoronoiNodeCountByModelId[runtimeModelId] = surfaceProduct.nodeCount;
                outConfig.receiverVoronoiSurfaceMappingBufferByModelId[runtimeModelId] = surfaceProduct.surfaceMappingBuffer;
                outConfig.receiverVoronoiSurfaceMappingBufferOffsetByModelId[runtimeModelId] = surfaceProduct.surfaceMappingBufferOffset;
                outConfig.receiverVoronoiSurfaceCellIndicesByModelId[runtimeModelId] = surfaceProduct.surfaceCellIndices;
                outConfig.receiverVoronoiSeedFlagsByModelId[runtimeModelId] = surfaceProduct.seedFlags;
            }
        }

        if (package.contactProduct.isValid()) {
            const ContactProduct* product = tryGetProduct<ContactProduct>(*ecsRegistry, package.contactProduct.outputSocketKey);
            if (!product) {
                return false;
            }

            outConfig.contactCouplings = { product->coupling };
        }

        outConfig.computeHash = buildComputeHash(outConfig);
        return true;
    }

    void removePublishedProduct(uint64_t socketKey) {
        if (socketKey == 0) {
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        ecsRegistry->remove<HeatProduct>(entity);
    }

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
