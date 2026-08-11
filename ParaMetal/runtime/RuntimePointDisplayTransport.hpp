#pragma once

#include "runtime/PointDisplayController.hpp"
#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <unordered_set>

class RuntimePointDisplayTransport {
public:
    void setController(PointDisplayController* updatedController) {
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
        for (const auto& [socketKey, package] : registry.points()) {
            if (!registry.isCurrent(socketKey)) continue;
            if (visibleKeys.find(socketKey) == visibleKeys.end()) {
                continue;
            }

            PointDisplayController::Config config{};
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
        const PointPackage& package,
        PointDisplayController::Config& outConfig) const {
        if (!controller || !products || socketKey == 0) {
            return false;
        }

        const PointProduct* product = products->resolve<PointProduct>(package.productHandle);
        if (!product || !product->isValid()) {
            return false;
        }

        outConfig = {};
        outConfig.vertexBuffer = product->positionBuffer;
        outConfig.vertexBufferOffset = product->positionBufferOffset;
        outConfig.pointCount = product->pointCount;
        outConfig.modelMatrix = product->modelMatrix;
        outConfig.displayHash = product->hashes.display;
        return true;
    }

    PointDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
