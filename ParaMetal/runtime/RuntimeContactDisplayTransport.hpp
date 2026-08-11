#pragma once

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/ContactDisplayController.hpp"
#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "util/Units.hpp"

#include <unordered_set>
#include <vector>

class RuntimeContactDisplayTransport {
public:
    void setController(ContactDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    void sync(const RuntimePackageManager& registry, const std::unordered_set<uint64_t>& visibleKeys) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;
        for (const auto& [socketKey, package] : registry.contacts()) {
            if (!registry.isCurrent(socketKey)) continue;
            if (visibleKeys.find(socketKey) == visibleKeys.end()) {
                continue;
            }

            ContactDisplayController::Config config{};
            if (!tryBuildConfig(socketKey, package, config)) {
                controller->remove(socketKey);
                continue;
            }

            controller->apply(socketKey, config);
            nextSocketKeys.insert(socketKey);
        }

        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                controller->remove(socketKey);
            }
        }
        activeSocketKeys = nextSocketKeys;
    }

    void finalizeSync() {
        if (!controller) {
            return;
        }

        controller->finalizeSync();
    }

private:
    bool tryBuildConfig(
        uint64_t socketKey,
        const ContactPackage& package,
        ContactDisplayController::Config& outConfig) const {
        if (!controller || !products || socketKey == 0) {
            return false;
        }
        if (!package.display.showContactLines) {
            return false;
        }

        const ContactProduct* computeProduct = products->resolve<ContactProduct>(package.productHandle);
        if (!computeProduct || !computeProduct->isValid()) {
            return false;
        }

        outConfig = {};
        outConfig.showContactLines = package.display.showContactLines;
        outConfig.authoredActive = package.authored.active;
        outConfig.hasValidContact = package.authored.pair.hasValidContact;
        outConfig.modelARuntimeModelId = computeProduct->modelARuntimeModelId;
        outConfig.modelBRuntimeModelId = computeProduct->modelBRuntimeModelId;
        outConfig.outlineVertices = computeProduct->outlineVertices;
        outConfig.correspondenceVertices = computeProduct->correspondenceVertices;
        outConfig.displayHash = buildDisplayHash(outConfig, computeProduct->hashes.display);
        return true;
    }

    ContactDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
