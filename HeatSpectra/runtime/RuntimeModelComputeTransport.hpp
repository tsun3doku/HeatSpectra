#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProductRegistry.hpp"

class ModelComputeRuntime;

class RuntimeModelComputeTransport {
public:
    void setRuntime(ModelComputeRuntime* updatedRuntime) {
        modelRuntime = updatedRuntime;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, ModelPackage>& packagesBySocket);
    void finalizeSync();

private:
    void applyPackage(uint64_t socketKey, const ModelPackage& package);
    void queuePublishedModel(uint64_t socketKey);
    void removePublishedProduct(uint64_t socketKey);
    void publishProduct(uint64_t socketKey, uint32_t runtimeModelId);

    ModelComputeRuntime* modelRuntime = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::vector<uint64_t> staleSocketKeys;
    std::vector<uint64_t> pendingPublishes;
};
