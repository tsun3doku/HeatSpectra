#include "RuntimeModelComputeTransport.hpp"

#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/ModelComputeRuntime.hpp"

void RuntimeModelComputeTransport::sync(const ECSRegistry& registry) {
    if (!modelRuntime) {
        return;
    }

    std::unordered_set<uint64_t> nextSocketKeys;

    auto view = registry.view<ModelPackage>();
    for (auto entity : view) {
        uint64_t socketKey = static_cast<uint64_t>(entity);
        const auto& package = registry.get<ModelPackage>(entity);
        nextSocketKeys.insert(socketKey);

        const ModelProduct* product = tryGetProduct<ModelProduct>(registry, socketKey);
        auto hashIt = appliedPackageHash.find(socketKey);
        if (product && hashIt != appliedPackageHash.end() && hashIt->second == package.packageHash) {
            continue;
        }

        applyPackage(socketKey, package);
    }

    for (uint64_t socketKey : activeSocketKeys) {
        if (nextSocketKeys.find(socketKey) != nextSocketKeys.end()) {
            continue;
        }

        modelRuntime->queueReleaseSocket(socketKey);
        appliedPackageHash.erase(socketKey);
    }

    activeSocketKeys = std::move(nextSocketKeys);
}

void RuntimeModelComputeTransport::finalizeSync() {
    if (!modelRuntime) {
        return;
    }

    modelRuntime->flush();

    std::vector<uint64_t> removals;
    auto productView = ecsRegistry->view<ModelProduct>();
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
        const auto& package = ecsRegistry->get<ModelPackage>(entity);
        auto hashIt = appliedPackageHash.find(socketKey);
        const ModelProduct* product = tryGetProduct<ModelProduct>(*ecsRegistry, socketKey);
        if (!product || hashIt == appliedPackageHash.end() || hashIt->second != package.packageHash) {
            uint32_t runtimeModelId = 0;
            if (modelRuntime->tryGetRuntimeModelId(socketKey, runtimeModelId) && runtimeModelId != 0) {
                publishProduct(socketKey, runtimeModelId);
            } else {
                removePublishedProduct(socketKey);
            }
        }
    }
}

void RuntimeModelComputeTransport::applyPackage(uint64_t socketKey, const ModelPackage& package) {
    if (!modelRuntime || socketKey == 0) {
        return;
    }

    modelRuntime->queueAcquireSocket(socketKey, package.geometry.baseModelPath);
}

void RuntimeModelComputeTransport::removePublishedProduct(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    auto entity = static_cast<ECSEntity>(socketKey);
    ecsRegistry->remove<ModelProduct>(entity);
}

void RuntimeModelComputeTransport::publishProduct(uint64_t socketKey, uint32_t runtimeModelId) {
    if (!modelRuntime || socketKey == 0 || runtimeModelId == 0) {
        return;
    }

    auto entity = static_cast<ECSEntity>(socketKey);
    const auto& package = ecsRegistry->get<ModelPackage>(entity);
    modelRuntime->setModelMatrix(runtimeModelId, NodeModelTransform::toMat4(package.localToWorld));

    ModelProduct product{};
    if (!modelRuntime->exportProduct(runtimeModelId, product)) {
        ecsRegistry->remove<ModelProduct>(entity);
        return;
    }

    ecsRegistry->emplace_or_replace<ModelProduct>(entity, product);
    appliedPackageHash[socketKey] = package.packageHash;
}
