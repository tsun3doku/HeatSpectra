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
        ContactDisplayController::Config config{};
        if (!tryBuildConfig(socketKey, package, config)) {
            controller->remove(socketKey);
            continue;
        }

        controller->apply(socketKey, config);
    }
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(uint64_t socketKey, const ContactPackage& package, ContactDisplayController::Config& outConfig) const {
        if (!controller || !ecsRegistry || socketKey == 0) {
            return false;
        }
        if (!package.display.showContactLines) {
            return false;
        }

        const ContactProduct* computeProduct = tryGetProduct<ContactProduct>(*ecsRegistry, socketKey);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }

        outConfig = {};
        outConfig.showContactLines = package.display.showContactLines;
        outConfig.authoredActive = package.authored.active;
        outConfig.hasValidContact = package.authored.pair.hasValidContact;
        outConfig.emitterRuntimeModelId = computeProduct->emitterRuntimeModelId;
        outConfig.receiverRuntimeModelId = computeProduct->receiverRuntimeModelId;
        outConfig.outlineVertices = computeProduct->outlineVertices;
        outConfig.correspondenceVertices = computeProduct->correspondenceVertices;
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->productHash);
        return true;
    }

    ContactDisplayController* controller = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
};
