#pragma once

#include <unordered_set>

#include "contact/ContactTypes.hpp"
#include "heat/HeatSystemController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

class RuntimeHeatTransport {
public:
    void setController(HeatSystemController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void apply(const HeatPackage* package) const {
        if (!controller) {
            return;
        }

        if (!package || !package->authored.active) {
            controller->disable();
            return;
        }

        HeatSystemController::Config config{};
        config.authored = package->authored;
        config.sourceGeometries.reserve(package->sourceRemeshProducts.size());
        config.sourceIntrinsicMeshes.reserve(package->sourceRemeshProducts.size());
        config.sourceRuntimeModelIds.reserve(package->sourceRemeshProducts.size());
        config.sourceTemperatureByRuntimeId.clear();
        std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
        for (size_t index = 0; index < package->sourceRemeshProducts.size(); ++index) {
            const ProductHandle& remeshProductHandle = package->sourceRemeshProducts[index];
            const RemeshProduct* product =
                productRegistry ? productRegistry->resolveRemesh(remeshProductHandle) : nullptr;
            if (!product || !product->isValid() || product->runtimeModelId == 0) {
                continue;
            }
            if (!seenSourceRuntimeModelIds.insert(product->runtimeModelId).second) {
                continue;
            }

            config.sourceGeometries.push_back(product->geometry);
            config.sourceIntrinsicMeshes.push_back(product->intrinsicMesh);
            config.sourceRuntimeModelIds.push_back(product->runtimeModelId);
            const float sourceTemperature =
                (index < package->sourceTemperatures.size())
                ? package->sourceTemperatures[index]
                : 100.0f;
            config.sourceTemperatureByRuntimeId[product->runtimeModelId] = sourceTemperature;
        }

        config.receiverGeometries.reserve(package->receiverRemeshProducts.size());
        config.receiverIntrinsicMeshes.reserve(package->receiverRemeshProducts.size());
        config.receiverRuntimeModelIds.reserve(package->receiverRemeshProducts.size());
        for (const ProductHandle& remeshProductHandle : package->receiverRemeshProducts) {
            const RemeshProduct* product =
                productRegistry ? productRegistry->resolveRemesh(remeshProductHandle) : nullptr;
            if (!product || !product->isValid() || product->runtimeModelId == 0) {
                continue;
            }

            config.receiverGeometries.push_back(product->geometry);
            config.receiverIntrinsicMeshes.push_back(product->intrinsicMesh);
            config.receiverRuntimeModelIds.push_back(product->runtimeModelId);
        }

        const size_t receiverCount = config.receiverRuntimeModelIds.size();
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
        for (size_t index = 0; index < package->receiverRemeshProducts.size() && receiverIndex < receiverCount; ++index) {
            const ProductHandle& remeshProductHandle = package->receiverRemeshProducts[index];
            const RemeshProduct* product =
                productRegistry ? productRegistry->resolveRemesh(remeshProductHandle) : nullptr;
            if (!product || !product->isValid() || product->runtimeModelId == 0) {
                continue;
            }

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
            ++receiverIndex;
        }
        config.runtimeThermalMaterials = package->runtimeThermalMaterials;

        if (productRegistry && package->voronoiProduct.isValid()) {
            if (const VoronoiProduct* product = productRegistry->resolve(package->voronoiProduct)) {
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

                for (const VoronoiReceiverProduct& receiverProduct : product->receiverProducts) {
                    const uint32_t runtimeModelId = receiverProduct.runtimeModelId;
                    if (runtimeModelId == 0) {
                        continue;
                    }

                    config.receiverVoronoiNodeOffsetByModelId[runtimeModelId] = receiverProduct.nodeOffset;
                    config.receiverVoronoiNodeCountByModelId[runtimeModelId] = receiverProduct.nodeCount;
                    config.receiverVoronoiSurfaceMappingBufferByModelId[runtimeModelId] = receiverProduct.surfaceMappingBuffer;
                    config.receiverVoronoiSurfaceMappingBufferOffsetByModelId[runtimeModelId] = receiverProduct.surfaceMappingBufferOffset;
                    config.receiverVoronoiSurfaceCellIndicesByModelId[runtimeModelId] = receiverProduct.surfaceCellIndices;
                    config.receiverVoronoiSeedFlagsByModelId[runtimeModelId] = receiverProduct.seedFlags;
                }
            }
        }

        if (productRegistry && package->contactProduct.isValid()) {
            if (const ContactProduct* product = productRegistry->resolveContact(package->contactProduct)) {
                config.contactCouplings = { *product };
            }
        }

        controller->configure(config);
    }

private:
    HeatSystemController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
};
