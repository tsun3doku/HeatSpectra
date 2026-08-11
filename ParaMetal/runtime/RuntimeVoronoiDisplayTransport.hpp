#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/VoronoiDisplayController.hpp"
#include "util/GeometryUtils.hpp"

#include <iostream>
#include <unordered_set>
#include <vector>

class RuntimeVoronoiDisplayTransport {
public:
    void setController(VoronoiDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    void sync(const RuntimePackageManager& registry, const std::unordered_set<uint64_t>& visibleKeys) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;
        for (const auto& [socketKey, package] : registry.voronois()) {
            if (!registry.isCurrent(socketKey)) continue;
            if (visibleKeys.find(socketKey) == visibleKeys.end()) {
                continue;
            }

            VoronoiDisplayController::Config config{};
            if (!tryBuildConfig(socketKey, package, config)) {
                controller->remove(socketKey);
                continue;
            }

            controller->apply(socketKey, config);
            nextSocketKeys.insert(socketKey);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                controller->remove(socketKey);
            }
        }
        activeSocketKeys = nextSocketKeys;
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(
        uint64_t socketKey,
        const VoronoiPackage& package,
        VoronoiDisplayController::Config& outConfig) const {
        if (!controller || !products || socketKey == 0) {
            return false;
        }
        if (!package.display.showVoronoi && !package.display.showPoints) {
            return false;
        }

        const VoronoiProduct* computeProduct = products->resolve<VoronoiProduct>(package.productHandle);
        if (!computeProduct) {
            std::cerr << "[VoronoiDisplayTransport] no computeProduct for socketKey=" << socketKey << std::endl;
            return false;
        }
        if (!computeProduct->isValid()) {
            std::cerr << "[VoronoiDisplayTransport] computeProduct invalid: candidateNodeCount=" << computeProduct->candidateNodeCount
                      << " nodeCount=" << computeProduct->nodeCount
                      << " candidateNodeBuffer=" << computeProduct->candidateNodeBuffer
                      << " nodeBuffer=" << computeProduct->nodeBuffer
                      << " neighborIndicesBuffer=" << computeProduct->candidateNeighborIndicesBuffer
                      << " seedPositionBuffer=" << computeProduct->seedPositionBuffer
                      << " candidateBuffer=" << computeProduct->candidateBuffer
                      << " isPointDomain=" << computeProduct->isPointDomain
                      << " runtimeModelId=" << computeProduct->runtimeModelId << std::endl;
            return false;
        }

        outConfig = {};
        outConfig.showVoronoi = package.display.showVoronoi && package.domainType != DomainType::Points;
        outConfig.showPoints = package.display.showPoints;
        outConfig.candidateNodeCount = computeProduct->candidateNodeCount;
        outConfig.mappedCandidateNodes = nullptr;
        outConfig.candidateNodeBuffer = computeProduct->candidateNodeBuffer;
        outConfig.candidateNodeBufferOffset = computeProduct->candidateNodeBufferOffset;
        outConfig.seedPositionBuffer = computeProduct->seedPositionBuffer;
        outConfig.seedPositionBufferOffset = computeProduct->seedPositionBufferOffset;
        outConfig.candidateNeighborIndicesBuffer = computeProduct->candidateNeighborIndicesBuffer;
        outConfig.candidateNeighborIndicesBufferOffset = computeProduct->candidateNeighborIndicesBufferOffset;
        outConfig.occupancyPointBuffer = computeProduct->occupancyPointBuffer;
        outConfig.occupancyPointBufferOffset = computeProduct->occupancyPointBufferOffset;
        outConfig.occupancyPointCount = computeProduct->occupancyPointCount;
        if (outConfig.showVoronoi) {
            const ModelProduct* modelProduct = products->resolve<ModelProduct>(package.modelProduct);
            const RemeshProduct* remeshProduct = products->resolve<RemeshProduct>(package.remeshProduct);
            if (!modelProduct) {
                std::cerr << "[VoronoiDisplayTransport] missing ModelProduct for modelMeshHandle=" << package.modelMeshHandle.key << std::endl;
                return false;
            }
            if (!remeshProduct) {
                std::cerr << "[VoronoiDisplayTransport] missing RemeshProduct for modelRemeshHandle=" << package.modelRemeshHandle.key << std::endl;
                return false;
            }
            if (modelProduct->runtimeModelId != computeProduct->runtimeModelId) {
                std::cerr << "[VoronoiDisplayTransport] runtimeModelId mismatch: model=" << modelProduct->runtimeModelId
                          << " compute=" << computeProduct->runtimeModelId << std::endl;
                return false;
            }

            outConfig.bindingKey = socketKey;
            outConfig.runtimeModelId = computeProduct->runtimeModelId;
            outConfig.candidateBuffer = computeProduct->candidateBuffer;
            outConfig.candidateBufferOffset = computeProduct->candidateBufferOffset;
            outConfig.supportingHalfedgeView = remeshProduct->supportingHalfedgeView;
            outConfig.supportingAngleView = remeshProduct->supportingAngleView;
            outConfig.halfedgeView = remeshProduct->halfedgeView;
            outConfig.edgeView = remeshProduct->edgeView;
            outConfig.triangleView = remeshProduct->triangleView;
            outConfig.lengthView = remeshProduct->lengthView;
            outConfig.inputHalfedgeView = remeshProduct->inputHalfedgeView;
            outConfig.inputEdgeView = remeshProduct->inputEdgeView;
            outConfig.inputTriangleView = remeshProduct->inputTriangleView;
            outConfig.inputLengthView = remeshProduct->inputLengthView;
            outConfig.intrinsicVertexCount = remeshProduct->intrinsicVertexCount;
            outConfig.vertexBuffer = modelProduct->vertexBuffer;
            outConfig.vertexBufferOffset = modelProduct->vertexBufferOffset;
            outConfig.indexBuffer = modelProduct->indexBuffer;
            outConfig.indexBufferOffset = modelProduct->indexBufferOffset;
            outConfig.indexCount = modelProduct->indexCount;
            outConfig.modelMatrix = toMat4(package.localToWorld);
            outConfig.canonicalToWorldScale = package.canonicalToWorldScale;
        }
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->hashes.display);
        return true;
    }

    VoronoiDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
