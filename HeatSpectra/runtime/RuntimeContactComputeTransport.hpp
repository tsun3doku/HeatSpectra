#pragma once

#include <unordered_set>
#include <vector>

#include "contact/ContactSystemComputeController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

class RuntimeContactComputeTransport {
public:
    void setController(ContactSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, ContactPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        staleSocketKeys.clear();
        std::unordered_set<uint64_t> nextSocketKeys;

        for (const auto& [socketKey, package] : packagesBySocket) {
            if (!package.authored.active || !package.authored.pair.hasValidContact) {
                continue;
            }

            ContactSystemComputeController::Config config{};
            bool isPackageReady = true;
            const RemeshProduct* emitterRemeshProduct =
                (productRegistry && package.emitterRemeshProduct.isValid())
                ? productRegistry->resolveRemesh(package.emitterRemeshProduct)
                : nullptr;
            const RemeshProduct* receiverRemeshProduct =
                (productRegistry && package.receiverRemeshProduct.isValid())
                ? productRegistry->resolveRemesh(package.receiverRemeshProduct)
                : nullptr;
            if (!emitterRemeshProduct || !receiverRemeshProduct) {
                isPackageReady = false;
            }

            const bool hasEmitterModelProduct =
                productRegistry &&
                package.emitterModelProduct.isValid() &&
                productRegistry->resolveModel(package.emitterModelProduct);
            const bool hasReceiverModelProduct =
                productRegistry &&
                package.receiverModelProduct.isValid() &&
                productRegistry->resolveModel(package.receiverModelProduct);
            if (!hasEmitterModelProduct || !hasReceiverModelProduct) {
                isPackageReady = false;
            }

            if (!isPackageReady) {
                const bool wasApplied = publishedSocketKeys.find(socketKey) != publishedSocketKeys.end();
                if (wasApplied) {
                    nextSocketKeys.insert(socketKey);
                }
                continue;
            }

            config.couplingType = package.authored.pair.kind;
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
                const bool wasApplied = publishedSocketKeys.find(socketKey) != publishedSocketKeys.end();
                if (wasApplied) {
                    nextSocketKeys.insert(socketKey);
                }
                continue;
            }

            controller->configure(socketKey, config);
            nextSocketKeys.insert(socketKey);
        }

        auto it = publishedSocketKeys.begin();
        while (it != publishedSocketKeys.end()) {
            if (nextSocketKeys.find(*it) == nextSocketKeys.end()) {
                controller->disable(*it);
                staleSocketKeys.push_back(*it);
                it = publishedSocketKeys.erase(it);
            } else {
                ++it;
            }
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

        publishedSocketKeys = activeSocketKeys;
    }

private:
    void removePublishedProduct(uint64_t socketKey) {
        if (productRegistry && socketKey != 0) {
            productRegistry->removeContact(socketKey);
        }
    }

    void publishProduct(uint64_t socketKey) {
        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        ContactProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            productRegistry->removeContact(socketKey);
            return;
        }

        productRegistry->publishContact(socketKey, product);
    }

    ContactSystemComputeController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_set<uint64_t> publishedSocketKeys;
    std::vector<uint64_t> staleSocketKeys;
};
