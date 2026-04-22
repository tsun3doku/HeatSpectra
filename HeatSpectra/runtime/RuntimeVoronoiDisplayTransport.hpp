#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/VoronoiDisplayController.hpp"

#include <unordered_set>
#include <vector>

class RuntimeVoronoiDisplayTransport {
public:
    void setController(VoronoiDisplayController* updatedController) {
        controller = updatedController;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void setVisibleKeys(const std::unordered_set<uint64_t>* keys) {
        visibleKeys = keys;
    }

    void sync(const ECSRegistry& registry) {
        if (!controller) {
            return;
        }

        auto view = registry.view<VoronoiPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            if (visibleKeys && visibleKeys->find(socketKey) == visibleKeys->end()) {
                continue;
            }

            const auto& package = registry.get<VoronoiPackage>(entity);
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    void applyPackage(uint64_t socketKey, const VoronoiPackage& package) {
        if (!controller || socketKey == 0) {
            return;
        }

        if (!package.display.showVoronoi && !package.display.showPoints) {
            controller->remove(socketKey);
            return;
        }

        const VoronoiProduct* computeProduct = tryGetProduct<VoronoiProduct>(*ecsRegistry, socketKey);
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
        config.displayHash = buildDisplayHash(config, computeProduct->productHash);

        controller->apply(socketKey, config);
    }

    VoronoiDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
