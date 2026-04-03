#pragma once

#include <iostream>

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

    static bool buildBinding(
        const ContactPackage& package,
        const RuntimeProductRegistry* productRegistry,
        RuntimeContactBinding& outBinding) {
        outBinding = {};
        if (!productRegistry ||
            !package.authored.active ||
            !package.authored.pair.hasValidContact) {
            return false;
        }

        const RemeshProduct* emitterRemeshProduct =
            package.emitterRemeshProduct.isValid() ? productRegistry->resolveRemesh(package.emitterRemeshProduct) : nullptr;
        const RemeshProduct* receiverRemeshProduct =
            package.receiverRemeshProduct.isValid() ? productRegistry->resolveRemesh(package.receiverRemeshProduct) : nullptr;

        if (!emitterRemeshProduct || !receiverRemeshProduct ||
            !emitterRemeshProduct->isValid() || !receiverRemeshProduct->isValid()) {
            return false;
        }

        outBinding.contactPair = package.authored.pair;
        outBinding.runtimePair.couplingType = package.authored.pair.kind;
        outBinding.runtimePair.minNormalDot = package.authored.pair.minNormalDot;
        outBinding.runtimePair.contactRadius = package.authored.pair.contactRadius;
        outBinding.runtimePair.emitter.geometry = emitterRemeshProduct->geometry;
        outBinding.runtimePair.emitter.intrinsicMesh = emitterRemeshProduct->intrinsicMesh;
        outBinding.runtimePair.receiver.geometry = receiverRemeshProduct->geometry;
        outBinding.runtimePair.receiver.intrinsicMesh = receiverRemeshProduct->intrinsicMesh;
        outBinding.emitterRuntimeModelId = emitterRemeshProduct->runtimeModelId;
        outBinding.receiverRuntimeModelId = receiverRemeshProduct->runtimeModelId;
        outBinding.receiverTriangleIndices = receiverRemeshProduct->intrinsicMesh.indices;

        return outBinding.emitterRuntimeModelId != 0 &&
            outBinding.receiverRuntimeModelId != 0 &&
            outBinding.emitterRuntimeModelId != outBinding.receiverRuntimeModelId &&
            !outBinding.receiverTriangleIndices.empty();
    }

    void apply(uint64_t socketKey, const ContactPackage* package) {
        if (!controller) {
            std::cerr << "[ContactTransport] No controller set" << std::endl;
            return;
        }

        if (!package ||
            !package->authored.active ||
            !package->authored.pair.hasValidContact) {
            controller->disable();
            removePublishedProduct();
            return;
        }

        ContactSystemController::Config config{};
        if (!buildBinding(*package, productRegistry, config.binding)) {
            std::cerr << "[ContactTransport] Disabling: failed to build runtime binding" << std::endl;
            controller->disable();
            removePublishedProduct();
            return;
        }

        controller->configure(config);
        publishProduct(socketKey);
    }

    void clearCache() const {
        if (controller) {
            controller->clearCache();
        }
    }

private:
    void removePublishedProduct() {
        if (!productRegistry || publishedSocketKey == 0) {
            publishedSocketKey = 0;
            return;
        }

        productRegistry->removeContact(publishedSocketKey);
        publishedSocketKey = 0;
    }

    void publishProduct(uint64_t socketKey) {
        if (publishedSocketKey != 0 && publishedSocketKey != socketKey) {
            removePublishedProduct();
        }

        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        ContactProduct product{};
        if (!controller->exportProduct(product)) {
            std::cerr << "[ContactTransport] exportProduct FAILED for socketKey=" << socketKey << std::endl;
            productRegistry->removeContact(socketKey);
            if (publishedSocketKey == socketKey) {
                publishedSocketKey = 0;
            }
            return;
        }

        productRegistry->publishContact(socketKey, product);
        publishedSocketKey = socketKey;
    }

    ContactSystemController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    uint64_t publishedSocketKey = 0;
};
