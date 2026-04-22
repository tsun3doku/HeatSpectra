#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/RuntimeECS.hpp"

#include <unordered_set>
#include <vector>

class RuntimeContactDisplayTransport {
public:
    void setController(ContactDisplayController* updatedController) {
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

        auto view = registry.view<ContactPackage>();
        for (auto entity : view) {
            uint64_t socketKey = static_cast<uint64_t>(entity);
            if (visibleKeys && visibleKeys->find(socketKey) == visibleKeys->end()) {
                continue;
            }

            const auto& package = registry.get<ContactPackage>(entity);
            applyPackage(socketKey, package);
        }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    void applyPackage(uint64_t socketKey, const ContactPackage& package) {
        if (!controller || socketKey == 0) {
            return;
        }

        if (!package.display.showContactLines) {
            controller->remove(socketKey);
            return;
        }

        const ContactProduct* computeProduct = tryGetProduct<ContactProduct>(*ecsRegistry, socketKey);
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
        config.displayHash = buildDisplayHash(config, computeProduct->productHash);

        controller->apply(socketKey, config);
    }

    ContactDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
