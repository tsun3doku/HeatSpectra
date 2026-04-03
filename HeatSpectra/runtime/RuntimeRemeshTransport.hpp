#pragma once

#include "runtime/RemeshController.hpp"
#include "runtime/RuntimeModelTransport.hpp"
#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

#include <unordered_set>

class RuntimeRemeshTransport {
public:
    void setController(RemeshController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void setModelTransport(RuntimeModelTransport* updatedModelTransport) {
        modelTransport = updatedModelTransport;
    }

    void sync(const std::unordered_map<uint64_t, RemeshPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> nextSocketKeys;
        nextSocketKeys.reserve(packagesBySocket.size());

        for (const auto& [socketKey, package] : packagesBySocket) {
            nextSocketKeys.insert(socketKey);
            applyPackage(socketKey, package);
        }

        std::vector<uint64_t> staleSocketKeys;
        staleSocketKeys.reserve(activeSocketKeys.size());
        for (uint64_t socketKey : activeSocketKeys) {
            if (nextSocketKeys.find(socketKey) == nextSocketKeys.end()) {
                staleSocketKeys.push_back(socketKey);
            }
        }

        for (uint64_t socketKey : staleSocketKeys) {
            controller->disable(socketKey);
            removePublishedProduct(socketKey);
            removePublishedModel(socketKey);
            activeSocketKeys.erase(socketKey);
        }
    }

private:
    void applyPackage(uint64_t socketKey, const RemeshPackage& package) {
        if (socketKey == 0 || package.sourceGeometry.modelId == 0) {
            return;
        }

        RemeshController::Config config{};
        config.socketKey = socketKey;
        config.sourceGeometry = package.sourceGeometry;
        config.params = package.params;
        config.remeshHandle = package.remeshHandle;
        controller->configure(config);
        publishProduct(socketKey);
        publishModel(socketKey);
        activeSocketKeys.insert(socketKey);
    }

    void removePublishedProduct(uint64_t socketKey) {
        if (!productRegistry || socketKey == 0) {
            return;
        }

        productRegistry->removeRemesh(socketKey);
    }

    void removePublishedModel(uint64_t socketKey) {
        if (!modelTransport || socketKey == 0) {
            return;
        }

        modelTransport->remove(socketKey);
    }

    void publishProduct(uint64_t socketKey) {
        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        RemeshProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            productRegistry->removeRemesh(socketKey);
            return;
        }

        productRegistry->publishRemesh(socketKey, product);
    }

    void publishModel(uint64_t socketKey) {
        if (!modelTransport || !controller || socketKey == 0) {
            return;
        }

        RemeshProduct product{};
        if (!controller->exportProduct(socketKey, product) || product.runtimeModelId == 0) {
            modelTransport->remove(socketKey);
            return;
        }

        modelTransport->publish(socketKey, product.runtimeModelId);
    }

    RemeshController* controller = nullptr;
    RuntimeModelTransport* modelTransport = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
