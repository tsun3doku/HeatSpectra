#pragma once

#include "runtime/RemeshController.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_set>
#include <vector>

class RuntimeRemeshComputeTransport {
public:
    void setController(RemeshController* updatedController) {
        controller = updatedController;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void sync(const ECSRegistry& registry) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;

        auto view = registry.view<RemeshPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            const auto& package = registry.get<RemeshPackage>(entity);
            nextSocketKeys.insert(socketKey);

            const RemeshProduct* product = tryGetProduct<RemeshProduct>(registry, socketKey);
            auto hashIt = appliedPackageHash.find(socketKey);
            if (product && hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
                continue;
            }

            applyPackage(socketKey, package);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                controller->disable(socketKey);
                appliedPackageHash.erase(socketKey);
            }
        }

        activeSocketKeys = std::move(nextSocketKeys);
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        std::vector<uint64_t> removals;
        auto productView = ecsRegistry->view<RemeshProduct>();
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
            const auto& package = ecsRegistry->get<RemeshPackage>(entity);
            auto hashIt = appliedPackageHash.find(socketKey);
            const RemeshProduct* product = tryGetProduct<RemeshProduct>(*ecsRegistry, socketKey);
            if (!product || hashIt == appliedPackageHash.end() || hashIt->second != package.packageHash) {
                publishProduct(socketKey);
            }
        }
    }

private:
    void applyPackage(uint64_t socketKey, const RemeshPackage& package) {
        if (socketKey == 0 || !controller) {
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
        const ModelProduct* modelProduct = tryGetProduct<ModelProduct>(*ecsRegistry, package.modelProductHandle.outputSocketKey);
        config.runtimeModelId = modelProduct ? modelProduct->runtimeModelId : 0;
        config.computeHash = buildComputeHash(config);
        controller->configure(config);
    }

    void removePublishedProduct(uint64_t socketKey) {
        if (socketKey == 0) {
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        ecsRegistry->remove<RemeshProduct>(entity);
    }

    void publishProduct(uint64_t socketKey) {
        if (!controller || socketKey == 0) {
            return;
        }

        RemeshProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            auto entity = static_cast<ECSEntity>(socketKey);
            ecsRegistry->remove<RemeshProduct>(entity);
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        const auto& package = ecsRegistry->get<RemeshPackage>(entity);
        ecsRegistry->emplace_or_replace<RemeshProduct>(entity, product);
        appliedPackageHash[socketKey] = package.packageHash;
    }

    RemeshController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_map<uint64_t, uint64_t> appliedPackageHash;
};
