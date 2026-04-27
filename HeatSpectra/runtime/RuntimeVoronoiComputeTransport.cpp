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
        auto hashIt = appliedPackageHash.find(socketKey);
        if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
            nextSocketKeys.insert(socketKey);
            continue;
        }

        VoronoiSystemComputeController::Config config{};
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

bool RuntimeVoronoiComputeTransport::tryBuildConfig(
    uint64_t socketKey,
    const VoronoiPackage& package,
    VoronoiSystemComputeController::Config& outConfig) const {
    if (socketKey == 0 || !ecsRegistry) {
        return false;
    }
    if (!package.authored.active || package.receiverRemeshProducts.empty()) {
        return false;
    }

    const size_t receiverCount = package.receiverRemeshProducts.size();
    if (package.receiverLocalToWorlds.size() < receiverCount ||
        package.receiverModelProducts.size() < receiverCount) {
        return false;
    }

    outConfig = {};
    outConfig.active = true;
    outConfig.cellSize = package.authored.params.cellSize;
    outConfig.voxelResolution = package.authored.params.voxelResolution;
    outConfig.receiverRuntimeModelIds.resize(receiverCount, 0);
    outConfig.receiverNodeModelIds.resize(receiverCount, 0);
    outConfig.receiverGeometryPositions.resize(receiverCount);
    outConfig.receiverGeometryTriangleIndices.resize(receiverCount);
    outConfig.receiverIntrinsicMeshes.resize(receiverCount);
    outConfig.receiverSurfaceVertices.resize(receiverCount);
    outConfig.receiverIntrinsicTriangleIndices.resize(receiverCount);
    outConfig.meshVertexBuffers.resize(receiverCount, VK_NULL_HANDLE);
    outConfig.meshVertexBufferOffsets.resize(receiverCount, 0);
    outConfig.meshIndexBuffers.resize(receiverCount, VK_NULL_HANDLE);
    outConfig.meshIndexBufferOffsets.resize(receiverCount, 0);
    outConfig.meshIndexCounts.resize(receiverCount, 0);
    outConfig.meshModelMatrices.resize(receiverCount, glm::mat4(1.0f));
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

    size_t remeshIndex = 0;
    for (; remeshIndex < receiverCount; ++remeshIndex) {
        const ProductHandle& remeshProductHandle = package.receiverRemeshProducts[remeshIndex];
        const RemeshProduct* product =
            tryGetProduct<RemeshProduct>(*ecsRegistry, remeshProductHandle.outputSocketKey);
        if (!product) {
            break;
        }

        outConfig.receiverRuntimeModelIds[remeshIndex] = product->runtimeModelId;
        outConfig.receiverNodeModelIds[remeshIndex] = 0;
        outConfig.receiverGeometryPositions[remeshIndex] = product->geometryPositions;
        outConfig.receiverGeometryTriangleIndices[remeshIndex] = product->geometryTriangleIndices;
        outConfig.receiverIntrinsicMeshes[remeshIndex] = product->intrinsicMesh;
        outConfig.receiverIntrinsicTriangleIndices[remeshIndex] = product->intrinsicMesh.indices;
        outConfig.receiverSurfaceVertices[remeshIndex].reserve(product->intrinsicMesh.vertices.size());
        for (const SupportingHalfedge::IntrinsicVertex& intrinsicVertex : product->intrinsicMesh.vertices) {
            VoronoiGeometryRuntime::SurfaceVertex vertex{};
            vertex.position = intrinsicVertex.position;
            vertex.normal = intrinsicVertex.normal;
            outConfig.receiverSurfaceVertices[remeshIndex].push_back(vertex);
        }
        outConfig.supportingHalfedgeViews[remeshIndex] = product->supportingHalfedgeView;
        outConfig.supportingAngleViews[remeshIndex] = product->supportingAngleView;
        outConfig.halfedgeViews[remeshIndex] = product->halfedgeView;
        outConfig.edgeViews[remeshIndex] = product->edgeView;
        outConfig.triangleViews[remeshIndex] = product->triangleView;
        outConfig.lengthViews[remeshIndex] = product->lengthView;
        outConfig.inputHalfedgeViews[remeshIndex] = product->inputHalfedgeView;
        outConfig.inputEdgeViews[remeshIndex] = product->inputEdgeView;
        outConfig.inputTriangleViews[remeshIndex] = product->inputTriangleView;
        outConfig.inputLengthViews[remeshIndex] = product->inputLengthView;
    }
    if (remeshIndex != receiverCount) {
        return false;
    }

    size_t modelIndex = 0;
    for (; modelIndex < receiverCount; ++modelIndex) {
        const ProductHandle& modelProductHandle = package.receiverModelProducts[modelIndex];
        const ModelProduct* modelProduct =
            tryGetProduct<ModelProduct>(*ecsRegistry, modelProductHandle.outputSocketKey);
        if (!modelProduct) {
            break;
        }

        outConfig.meshVertexBuffers[modelIndex] = modelProduct->vertexBuffer;
        outConfig.meshVertexBufferOffsets[modelIndex] = modelProduct->vertexBufferOffset;
        outConfig.meshIndexBuffers[modelIndex] = modelProduct->indexBuffer;
        outConfig.meshIndexBufferOffsets[modelIndex] = modelProduct->indexBufferOffset;
        outConfig.meshIndexCounts[modelIndex] = modelProduct->indexCount;
        outConfig.meshModelMatrices[modelIndex] = NodeModelTransform::toMat4(package.receiverLocalToWorlds[modelIndex]);
    }
    if (modelIndex != receiverCount) {
        return false;
    }

    outConfig.computeHash = buildComputeHash(outConfig);
    return true;
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
