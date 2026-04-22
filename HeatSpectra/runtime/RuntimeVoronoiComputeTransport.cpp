#include "RuntimeVoronoiComputeTransport.hpp"
#include "heat/VoronoiSystemComputeController.hpp"

void RuntimeVoronoiComputeTransport::sync(const ECSRegistry& registry) {
    if (!controller) {
        return;
    }

    std::unordered_set<uint64_t> nextSocketKeys;

    auto view = registry.view<VoronoiPackage>();
    for (auto entity : view) {
        uint64_t socketKey = static_cast<uint64_t>(entity);
        const auto& package = registry.get<VoronoiPackage>(entity);
        if (!package.authored.active || package.receiverRemeshProducts.empty()) {
            continue;
        }

        auto hashIt = appliedPackageHash.find(socketKey);
        if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
            nextSocketKeys.insert(socketKey);
            continue;
        }

        VoronoiSystemComputeController::Config config{};
        config.active = true;
        config.cellSize = package.authored.params.cellSize;
        config.voxelResolution = package.authored.params.voxelResolution;
        const size_t receiverCount = package.receiverRemeshProducts.size();
        if (package.receiverLocalToWorlds.size() < receiverCount ||
            package.receiverModelProducts.size() < receiverCount) {
            continue;
        }
        config.receiverRuntimeModelIds.resize(receiverCount, 0);
        config.receiverNodeModelIds.resize(receiverCount, 0);
        config.receiverGeometryPositions.resize(receiverCount);
        config.receiverGeometryTriangleIndices.resize(receiverCount);
        config.receiverIntrinsicMeshes.resize(receiverCount);
        config.receiverSurfaceVertices.resize(receiverCount);
        config.receiverIntrinsicTriangleIndices.resize(receiverCount);
        config.meshVertexBuffers.resize(receiverCount, VK_NULL_HANDLE);
        config.meshVertexBufferOffsets.resize(receiverCount, 0);
        config.meshIndexBuffers.resize(receiverCount, VK_NULL_HANDLE);
        config.meshIndexBufferOffsets.resize(receiverCount, 0);
        config.meshIndexCounts.resize(receiverCount, 0);
        config.meshModelMatrices.resize(receiverCount, glm::mat4(1.0f));
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

        size_t remeshIndex = 0;
        for (; remeshIndex < receiverCount; ++remeshIndex) {
            const ProductHandle& remeshProductHandle = package.receiverRemeshProducts[remeshIndex];
            const RemeshProduct* product =
                tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
            if (!product) {
                break;
            }

            config.receiverRuntimeModelIds[remeshIndex] = product->runtimeModelId;
            config.receiverNodeModelIds[remeshIndex] = 0;
            config.receiverGeometryPositions[remeshIndex] = product->geometryPositions;
            config.receiverGeometryTriangleIndices[remeshIndex] = product->geometryTriangleIndices;
            config.receiverIntrinsicMeshes[remeshIndex] = product->intrinsicMesh;
            config.receiverIntrinsicTriangleIndices[remeshIndex] = product->intrinsicMesh.indices;
            config.receiverSurfaceVertices[remeshIndex].reserve(product->intrinsicMesh.vertices.size());
            for (const SupportingHalfedge::IntrinsicVertex& intrinsicVertex : product->intrinsicMesh.vertices) {
                VoronoiGeometryRuntime::SurfaceVertex vertex{};
                vertex.position = intrinsicVertex.position;
                vertex.normal = intrinsicVertex.normal;
                config.receiverSurfaceVertices[remeshIndex].push_back(vertex);
            }
            config.supportingHalfedgeViews[remeshIndex] = product->supportingHalfedgeView;
            config.supportingAngleViews[remeshIndex] = product->supportingAngleView;
            config.halfedgeViews[remeshIndex] = product->halfedgeView;
            config.edgeViews[remeshIndex] = product->edgeView;
            config.triangleViews[remeshIndex] = product->triangleView;
            config.lengthViews[remeshIndex] = product->lengthView;
            config.inputHalfedgeViews[remeshIndex] = product->inputHalfedgeView;
            config.inputEdgeViews[remeshIndex] = product->inputEdgeView;
            config.inputTriangleViews[remeshIndex] = product->inputTriangleView;
            config.inputLengthViews[remeshIndex] = product->inputLengthView;
        }
        if (remeshIndex != receiverCount) {
            continue;
        }

        size_t modelIndex = 0;
        for (; modelIndex < receiverCount; ++modelIndex) {
            const ProductHandle& modelProductHandle = package.receiverModelProducts[modelIndex];
            const ModelProduct* modelProduct =
                tryGetProduct<ModelProduct>(*ecsRegistry, modelProductHandle.outputSocketKey);
            if (!modelProduct) {
                break;
            }

            config.meshVertexBuffers[modelIndex] = modelProduct->vertexBuffer;
            config.meshVertexBufferOffsets[modelIndex] = modelProduct->vertexBufferOffset;
            config.meshIndexBuffers[modelIndex] = modelProduct->indexBuffer;
            config.meshIndexBufferOffsets[modelIndex] = modelProduct->indexBufferOffset;
            config.meshIndexCounts[modelIndex] = modelProduct->indexCount;
            config.meshModelMatrices[modelIndex] = NodeModelTransform::toMat4(package.receiverLocalToWorlds[modelIndex]);
        }
        if (modelIndex != receiverCount) {
            continue;
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

void RuntimeVoronoiComputeTransport::finalizeSync() {
    if (!controller) {
        return;
    }

    std::vector<uint64_t> removals;
    auto productView = ecsRegistry->view<VoronoiProduct>();
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
        const auto& package = ecsRegistry->get<VoronoiPackage>(entity);
        auto hashIt = appliedPackageHash.find(socketKey);
        const VoronoiProduct* product = tryGetProduct<VoronoiProduct>(*ecsRegistry, socketKey);
        if (!product || hashIt == appliedPackageHash.end() || hashIt->second != package.packageHash) {
            publishProduct(socketKey);
        }
    }
}

void RuntimeVoronoiComputeTransport::removePublishedProduct(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    auto entity = static_cast<ECSEntity>(socketKey);
    ecsRegistry->remove<VoronoiProduct>(entity);
}

void RuntimeVoronoiComputeTransport::publishProduct(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }

    VoronoiProduct product{};
    if (!controller->exportProduct(socketKey, product)) {
        auto entity = static_cast<ECSEntity>(socketKey);
        ecsRegistry->remove<VoronoiProduct>(entity);
        return;
    }

    auto entity = static_cast<ECSEntity>(socketKey);
    const auto& package = ecsRegistry->get<VoronoiPackage>(entity);
    ecsRegistry->emplace_or_replace<VoronoiProduct>(entity, product);
    appliedPackageHash[socketKey] = package.packageHash;
}
