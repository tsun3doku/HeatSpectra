#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/VoronoiDisplayController.hpp"

#include <unordered_map>

class RuntimeVoronoiDisplayTransport {
public:
    void setController(VoronoiDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        computeProductRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, VoronoiPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        for (const auto& [socketKey, package] : packagesBySocket) {
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (controller) {
            controller->finalizeSync();
        }
    }

private:
    void applyPackage(uint64_t socketKey, const VoronoiPackage& package) {
        if (!controller || !computeProductRegistry || socketKey == 0) {
            return;
        }

        if (!package.display.showVoronoi && !package.display.showPoints) {
            controller->remove(socketKey);
            return;
        }

        ProductHandle computeHandle =
            computeProductRegistry->getPublishedHandle(NodeProductType::Voronoi, socketKey);
        const VoronoiProduct* computeProduct =
            computeProductRegistry->resolveVoronoi(computeHandle);
        if (!computeProduct || !computeProduct->isValid()) {
            controller->remove(socketKey);
            return;
        }

        VoronoiDisplayController::Config config{};
        config.showVoronoi = package.display.showVoronoi;
        config.showPoints = package.display.showPoints;
        config.nodeCount = computeProduct->nodeCount;
        config.mappedVoronoiNodes = computeProduct->mappedVoronoiNodes;
        config.nodeBuffer = computeProduct->nodeBuffer;
        config.nodeBufferOffset = computeProduct->nodeBufferOffset;
        config.seedPositionBuffer = computeProduct->seedPositionBuffer;
        config.seedPositionBufferOffset = computeProduct->seedPositionBufferOffset;
        config.neighborIndicesBuffer = computeProduct->neighborIndicesBuffer;
        config.neighborIndicesBufferOffset = computeProduct->neighborIndicesBufferOffset;
        config.occupancyPointBuffer = computeProduct->occupancyPointBuffer;
        config.occupancyPointBufferOffset = computeProduct->occupancyPointBufferOffset;
        config.occupancyPointCount = computeProduct->occupancyPointCount;
        if (package.display.showVoronoi) {
            config.surfaces = computeProduct->surfaces;
        }
        config.contentHash = computeContentHash(config);

        controller->apply(socketKey, config);
    }

    VoronoiDisplayController* controller = nullptr;
    RuntimeProductRegistry* computeProductRegistry = nullptr;
};
