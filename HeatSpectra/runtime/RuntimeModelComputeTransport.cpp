#include "RuntimeModelComputeTransport.hpp"

#include "runtime/ModelComputeRuntime.hpp"

void RuntimeModelComputeTransport::sync(const std::unordered_map<uint64_t, ModelPackage>& packagesBySocket) {
    if (!modelRuntime) {
        return;
    }

    staleSocketKeys.clear();
    pendingPublishes.clear();
    std::unordered_set<uint64_t> nextSocketKeys;

    for (const auto& [socketKey, package] : packagesBySocket) {
        nextSocketKeys.insert(socketKey);
        applyPackage(socketKey, package);
    }

    for (uint64_t socketKey : activeSocketKeys) {
        if (nextSocketKeys.find(socketKey) != nextSocketKeys.end()) {
            continue;
        }

        modelRuntime->queueReleaseSocket(socketKey);
        staleSocketKeys.push_back(socketKey);
    }

    activeSocketKeys.clear();
    for (uint64_t socketKey : nextSocketKeys) {
        activeSocketKeys.insert(socketKey);
    }
}

void RuntimeModelComputeTransport::finalizeSync() {
    if (!modelRuntime) {
        return;
    }

    modelRuntime->flush();

    for (uint64_t socketKey : staleSocketKeys) {
        removePublishedProduct(socketKey);
    }
    staleSocketKeys.clear();

    for (uint64_t socketKey : pendingPublishes) {
        uint32_t runtimeModelId = 0;
        if (modelRuntime->tryGetRuntimeModelId(socketKey, runtimeModelId) && runtimeModelId != 0) {
            publishProduct(socketKey, runtimeModelId);
        } else {
            removePublishedProduct(socketKey);
        }
    }
    pendingPublishes.clear();
}

void RuntimeModelComputeTransport::applyPackage(uint64_t socketKey, const ModelPackage& package) {
    if (!modelRuntime || socketKey == 0) {
        return;
    }

    modelRuntime->queueAcquireSocket(socketKey, package.geometry.baseModelPath);
    queuePublishedModel(socketKey);
}

void RuntimeModelComputeTransport::queuePublishedModel(uint64_t socketKey) {
    pendingPublishes.push_back(socketKey);
}

void RuntimeModelComputeTransport::removePublishedProduct(uint64_t socketKey) {
    if (!productRegistry || socketKey == 0) {
        return;
    }

    productRegistry->removeModel(socketKey);
}

void RuntimeModelComputeTransport::publishProduct(uint64_t socketKey, uint32_t runtimeModelId) {
    if (!productRegistry || !modelRuntime || socketKey == 0 || runtimeModelId == 0) {
        return;
    }

    ModelProduct product{};
    if (!modelRuntime->exportProduct(runtimeModelId, product)) {
        productRegistry->removeModel(socketKey);
        return;
    }

    productRegistry->publishModel(socketKey, product);
}
