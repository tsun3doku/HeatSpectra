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
        VoronoiDisplayController::Config config{};
        if (!tryBuildConfig(socketKey, package, config)) {
            controller->remove(socketKey);
            continue;
        }

        controller->apply(socketKey, config);
    }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(uint64_t socketKey, const VoronoiPackage& package, VoronoiDisplayController::Config& outConfig) const {
        if (!controller || !ecsRegistry || socketKey == 0) {
            return false;
        }
        if (!package.display.showVoronoi && !package.display.showPoints) {
            return false;
        }

        const VoronoiProduct* computeProduct = tryGetProduct<VoronoiProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }

        outConfig = {};
        outConfig.showVoronoi = package.display.showVoronoi;
        outConfig.showPoints = package.display.showPoints;
        outConfig.nodeCount = computeProduct->nodeCount;
        outConfig.mappedVoronoiNodes = computeProduct->mappedVoronoiNodes;
        outConfig.nodeBuffer = computeProduct->nodeBuffer;
        outConfig.nodeBufferOffset = computeProduct->nodeBufferOffset;
        outConfig.seedPositionBuffer = computeProduct->seedPositionBuffer;
        outConfig.seedPositionBufferOffset = computeProduct->seedPositionBufferOffset;
        outConfig.neighborIndicesBuffer = computeProduct->neighborIndicesBuffer;
        outConfig.neighborIndicesBufferOffset = computeProduct->neighborIndicesBufferOffset;
        outConfig.occupancyPointBuffer = computeProduct->occupancyPointBuffer;
        outConfig.occupancyPointBufferOffset = computeProduct->occupancyPointBufferOffset;
        outConfig.occupancyPointCount = computeProduct->occupancyPointCount;
        if (package.display.showVoronoi) {
            outConfig.surfaces = computeProduct->surfaces;
        }
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->productHash);
        return true;
    }

    VoronoiDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
