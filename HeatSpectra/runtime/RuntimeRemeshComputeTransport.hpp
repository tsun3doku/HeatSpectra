#pragma once

#include "runtime/RemeshController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_set>

class RuntimeRemeshComputeTransport {
public:
    void setController(RemeshController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, RemeshPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        staleSocketKeys.clear();
        std::unordered_set<uint64_t> nextSocketKeys;
        nextSocketKeys.reserve(packagesBySocket.size());

        for (const auto& [socketKey, package] : packagesBySocket) {
            nextSocketKeys.insert(socketKey);
            applyPackage(socketKey, package);
        }

        std::vector<uint64_t> staleSocketKeys;
        staleSocketKeys.reserve(activeSocketKeys.size());
        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                staleSocketKeys.push_back(socketKey);
            }
        }

        for (uint64_t socketKey : staleSocketKeys) {
            controller->disable(socketKey);
            this->staleSocketKeys.push_back(socketKey);
        }

        activeSocketKeys.clear();
        for (uint64_t nextSocketKey : nextSocketKeys) {
            activeSocketKeys.insert(nextSocketKey);
        }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        for (uint64_t socketKey : staleSocketKeys) {
            removePublishedProduct(socketKey);
        }
        staleSocketKeys.clear();

        for (uint64_t socketKey : activeSocketKeys) {
            publishProduct(socketKey);
        }
    }

private:
    void applyPackage(uint64_t socketKey, const RemeshPackage& package) {
        if (socketKey == 0) {
            return;
        }

        RemeshController::Config config{};
        config.socketKey = socketKey;
        config.pointPositions = package.sourceGeometry.pointPositions;
        config.triangleIndices = package.sourceGeometry.triangleIndices;
        config.iterations = package.params.iterations;
        config.minAngleDegrees = package.params.minAngleDegrees;
        config.maxEdgeLength = package.params.maxEdgeLength;
        config.stepSize = package.params.stepSize;
        if (productRegistry) {
            const ModelProduct* modelProduct = productRegistry->resolveModel(package.modelProductHandle);
            config.runtimeModelId = modelProduct ? modelProduct->runtimeModelId : 0;
        }
        controller->configure(config);
        activeSocketKeys.insert(socketKey);
    }

    void removePublishedProduct(uint64_t socketKey) {
        if (!productRegistry || socketKey == 0) {
            return;
        }

        productRegistry->removeRemesh(socketKey);
    }

    void publishProduct(uint64_t socketKey) {
        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        RemeshProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            productRegistry->removeRemesh(socketKey);
            return;
        }

        productRegistry->publishRemesh(socketKey, product);
    }

    RemeshController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::vector<uint64_t> staleSocketKeys;
};
