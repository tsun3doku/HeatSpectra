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
        outConfig.showVoronoi = package.display.showVoronoi &&
            package.domainType == DomainType::Global &&
            computeProduct->candidateNodeCount != 0 &&
            computeProduct->candidateNodeBuffer != VK_NULL_HANDLE;
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
            if (package.globalRemeshProducts.size() != computeProduct->globalDisplayCandidateBuffers.size()) {
                std::cerr << "[VoronoiDisplayTransport] display instance mismatch for socketKey=" << socketKey
                          << " remeshes=" << package.globalRemeshProducts.size()
                          << " display=" << computeProduct->globalDisplayCandidateBuffers.size() << std::endl;
                return false;
            }
            for (size_t i = 0; i < package.globalRemeshProducts.size(); ++i) {
                const RemeshProduct* remeshProduct = products->resolve<RemeshProduct>(package.globalRemeshProducts[i]);
                const ModelProduct* modelProduct = products->resolve<ModelProduct>(package.globalModelProducts[i]);
                if (!remeshProduct || !modelProduct) {
                    std::cerr << "[VoronoiDisplayTransport] missing remesh/model product for global instance=" << i << std::endl;
                    return false;
                }

                outConfig.modelRuntimeIds.push_back(remeshProduct->runtimeModelId);
                outConfig.candidateBuffers.push_back(computeProduct->globalDisplayCandidateBuffers[i]);
                outConfig.candidateBufferOffsets.push_back(computeProduct->globalDisplayCandidateBufferOffsets[i]);
                outConfig.modelBufferViews.push_back({
                    remeshProduct->supportingHalfedgeView,
                    remeshProduct->supportingAngleView,
                    remeshProduct->halfedgeView,
                    remeshProduct->edgeView,
                    remeshProduct->triangleView,
                    remeshProduct->lengthView,
                    remeshProduct->inputHalfedgeView,
                    remeshProduct->inputEdgeView,
                    remeshProduct->inputTriangleView,
                    remeshProduct->inputLengthView,
                });
                outConfig.intrinsicVertexCounts.push_back(remeshProduct->intrinsicVertexCount);
                outConfig.modelVertexBuffers.push_back(modelProduct->vertexBuffer);
                outConfig.modelVertexBufferOffsets.push_back(modelProduct->vertexBufferOffset);
                outConfig.modelIndexBuffers.push_back(modelProduct->indexBuffer);
                outConfig.modelIndexBufferOffsets.push_back(modelProduct->indexBufferOffset);
                outConfig.modelIndexCounts.push_back(modelProduct->indexCount);
                outConfig.modelMatrices.push_back(toMat4(package.globalRemeshLocalToWorld[i]));
                outConfig.modelCanonicalToWorldScales.push_back(package.canonicalToWorldScale);
            }
        }
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->hashes.display);
        return true;
    }

    VoronoiDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
