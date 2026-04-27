#pragma once

#include <unordered_set>
#include <vector>

#include "contact/ContactSystemComputeController.hpp"
#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

class RuntimeContactComputeTransport {
public:
    void setController(ContactSystemComputeController* updatedController) {
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

        auto view = registry.view<ContactPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            const auto& package = registry.get<ContactPackage>(entity);
            auto hashIt = appliedPackageHash.find(socketKey);
            if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
                nextSocketKeys.insert(socketKey);
                continue;
            }

            ContactSystemComputeController::Config config{};
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

    void finalizeSync() {
        if (!controller) {
            return;
        }

        std::vector<uint64_t> removals;
        auto productView = ecsRegistry->view<ContactProduct>();
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
            const auto& package = ecsRegistry->get<ContactPackage>(entity);
            auto hashIt = appliedPackageHash.find(socketKey);
            const ContactProduct* product = tryGetProduct<ContactProduct>(*ecsRegistry, socketKey);
            if (!product || hashIt == appliedPackageHash.end() || hashIt->second != package.packageHash) {
                publishProduct(socketKey);
            }
        }
    }

private:
    bool tryBuildConfig(uint64_t socketKey, const ContactPackage& package, ContactSystemComputeController::Config& outConfig) const {
        if (socketKey == 0 || !ecsRegistry) {
            return false;
        }
        if (!package.authored.active || !package.authored.pair.hasValidContact) {
            return false;
        }

        const RemeshProduct* emitterRemeshProduct =
            package.emitterRemeshProduct.isValid()
            ? tryGetProduct<RemeshProduct>(*ecsRegistry, package.emitterRemeshProduct.outputSocketKey)
            : nullptr;
        const RemeshProduct* receiverRemeshProduct =
            package.receiverRemeshProduct.isValid()
            ? tryGetProduct<RemeshProduct>(*ecsRegistry, package.receiverRemeshProduct.outputSocketKey)
            : nullptr;
        if (!emitterRemeshProduct || !receiverRemeshProduct) {
            return false;
        }

        outConfig = {};
        outConfig.couplingType = package.authored.pair.type;
        outConfig.minNormalDot = package.authored.pair.minNormalDot;
        outConfig.contactRadius = package.authored.pair.contactRadius;
        outConfig.emitterModelId = emitterRemeshProduct->runtimeModelId;
        outConfig.emitterLocalToWorld = package.emitterLocalToWorld;
        outConfig.emitterIntrinsicMesh = emitterRemeshProduct->intrinsicMesh;
        outConfig.receiverModelId = receiverRemeshProduct->runtimeModelId;
        outConfig.receiverLocalToWorld = package.receiverLocalToWorld;
        outConfig.receiverIntrinsicMesh = receiverRemeshProduct->intrinsicMesh;
        outConfig.emitterRuntimeModelId = emitterRemeshProduct->runtimeModelId;
        outConfig.receiverRuntimeModelId = receiverRemeshProduct->runtimeModelId;
        outConfig.receiverTriangleIndices = receiverRemeshProduct->intrinsicMesh.indices;
        if (!outConfig.isValid()) {
            return false;
        }

        outConfig.computeHash = buildComputeHash(outConfig);
        return true;
    }

    void removePublishedProduct(uint64_t socketKey) {
        if (socketKey == 0) {
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        ecsRegistry->remove<ContactProduct>(entity);
    }

    void publishProduct(uint64_t socketKey) {
        if (!controller || socketKey == 0) {
            return;
        }

        ContactProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            auto entity = static_cast<ECSEntity>(socketKey);
            ecsRegistry->remove<ContactProduct>(entity);
            return;
        }

        auto entity = static_cast<ECSEntity>(socketKey);
        const auto& package = ecsRegistry->get<ContactPackage>(entity);
        ecsRegistry->emplace_or_replace<ContactProduct>(entity, product);
        appliedPackageHash[socketKey] = package.packageHash;
    }

    ContactSystemComputeController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_map<uint64_t, uint64_t> appliedPackageHash;
};
