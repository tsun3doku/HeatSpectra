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
            if (!package.authored.active || !package.authored.pair.hasValidContact) {
                continue;
            }

            auto hashIt = appliedPackageHash.find(socketKey);
            if (hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
                nextSocketKeys.insert(socketKey);
                continue;
            }

            ContactSystemComputeController::Config config{};
            const RemeshProduct* emitterRemeshProduct =
                package.emitterRemeshProduct.isValid()
                ? tryGetProduct<RemeshProduct>(*ecsRegistry, package.emitterRemeshProduct.outputSocketKey)
                : nullptr;
            const RemeshProduct* receiverRemeshProduct =
                package.receiverRemeshProduct.isValid()
                ? tryGetProduct<RemeshProduct>(*ecsRegistry, package.receiverRemeshProduct.outputSocketKey)
                : nullptr;
            if (!emitterRemeshProduct || !receiverRemeshProduct) {
                continue;
            }

            const bool hasEmitterModelProduct =
                package.emitterModelProduct.isValid() &&
                tryGetProduct<ModelProduct>(*ecsRegistry, package.emitterModelProduct.outputSocketKey);
            const bool hasReceiverModelProduct =
                package.receiverModelProduct.isValid() &&
                tryGetProduct<ModelProduct>(*ecsRegistry, package.receiverModelProduct.outputSocketKey);
            if (!hasEmitterModelProduct || !hasReceiverModelProduct) {
                continue;
            }

            config.couplingType = package.authored.pair.type;
            config.minNormalDot = package.authored.pair.minNormalDot;
            config.contactRadius = package.authored.pair.contactRadius;
            config.emitterModelId = emitterRemeshProduct->runtimeModelId;
            config.emitterLocalToWorld = package.emitterLocalToWorld;
            config.emitterIntrinsicMesh = emitterRemeshProduct->intrinsicMesh;
            config.receiverModelId = receiverRemeshProduct->runtimeModelId;
            config.receiverLocalToWorld = package.receiverLocalToWorld;
            config.receiverIntrinsicMesh = receiverRemeshProduct->intrinsicMesh;
            config.emitterRuntimeModelId = emitterRemeshProduct->runtimeModelId;
            config.receiverRuntimeModelId = receiverRemeshProduct->runtimeModelId;
            config.receiverTriangleIndices = receiverRemeshProduct->intrinsicMesh.indices;

            if (!config.isValid()) {
                continue;
            }

            config.computeHash = buildComputeHash(config);
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
