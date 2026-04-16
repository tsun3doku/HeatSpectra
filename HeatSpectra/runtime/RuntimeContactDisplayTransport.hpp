#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

#include <unordered_map>

class RuntimeContactDisplayTransport {
public:
    void setController(ContactDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        computeProductRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, ContactPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        for (const auto& [socketKey, package] : packagesBySocket) {
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (controller) {
            controller->finalizeSync();
        }
    }

private:
    void applyPackage(uint64_t socketKey, const ContactPackage& package) {
        if (!controller || !computeProductRegistry || socketKey == 0) {
            return;
        }

        if (!package.display.showContactLines) {
            controller->remove(socketKey);
            return;
        }

        const ProductHandle contactHandle =
            computeProductRegistry->getPublishedHandle(NodeProductType::Contact, socketKey);
        const ContactProduct* computeProduct =
            computeProductRegistry->resolveContact(contactHandle);
        if (!computeProduct || !computeProduct->isValid()) {
            controller->remove(socketKey);
            return;
        }

        ContactDisplayController::Config config{};
        config.showContactLines = package.display.showContactLines;
        config.authoredActive = package.authored.active;
        config.hasValidContact = package.authored.pair.hasValidContact;
        config.emitterRuntimeModelId = computeProduct->emitterRuntimeModelId;
        config.receiverRuntimeModelId = computeProduct->receiverRuntimeModelId;
        config.outlineVertices = computeProduct->outlineVertices;
        config.correspondenceVertices = computeProduct->correspondenceVertices;
        config.contentHash = computeContentHash(config);

        controller->apply(socketKey, config);
    }

    ContactDisplayController* controller = nullptr;
    RuntimeProductRegistry* computeProductRegistry = nullptr;
};
