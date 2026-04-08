#pragma once

#include <unordered_set>

#include "contact/ContactSystemController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

class RuntimeContactTransport {
public:
    void setController(ContactSystemController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, ContactPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> activeSockets;

        for (const auto& [socketKey, package] : packagesBySocket) {
            if (!package.authored.active || !package.authored.pair.hasValidContact) {
                continue;
            }

            ContactSystemController::Config config{};
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

            if (!isPackageReady) {
                const bool wasApplied = publishedSocketKeys.find(socketKey) != publishedSocketKeys.end();
                if (wasApplied) {
                    activeSockets.insert(socketKey);
                }
                continue;
            }

            config.couplingType = package.authored.pair.kind;
            config.minNormalDot = package.authored.pair.minNormalDot;
            config.contactRadius = package.authored.pair.contactRadius;
            config.emitterModelId = package.emitterGeometry.modelId;
            config.emitterLocalToWorld = package.emitterGeometry.localToWorld;
            config.emitterIntrinsicMesh = emitterRemeshProduct->intrinsicMesh;
            config.receiverModelId = package.receiverGeometry.modelId;
            config.receiverLocalToWorld = package.receiverGeometry.localToWorld;
            config.receiverIntrinsicMesh = receiverRemeshProduct->intrinsicMesh;
            config.emitterRuntimeModelId = emitterRemeshProduct->runtimeModelId;
            config.receiverRuntimeModelId = receiverRemeshProduct->runtimeModelId;
            config.receiverTriangleIndices = receiverRemeshProduct->intrinsicMesh.indices;

            if (!config.isValid()) {
                const bool wasApplied = publishedSocketKeys.find(socketKey) != publishedSocketKeys.end();
                if (wasApplied) {
                    activeSockets.insert(socketKey);
                }
                continue;
            }

            controller->configure(socketKey, config);
            activeSockets.insert(socketKey);
            publishProduct(socketKey);
        }

        auto it = publishedSocketKeys.begin();
        while (it != publishedSocketKeys.end()) {
            if (activeSockets.find(*it) == activeSockets.end()) {
                controller->disable(*it);
                removePublishedProduct(*it);
                it = publishedSocketKeys.erase(it);
            } else {
                ++it;
            }
        }

        publishedSocketKeys = activeSockets;
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

    ContactSystemController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> publishedSocketKeys;
};
