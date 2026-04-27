#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

class ModelComputeRuntime;

class RuntimeModelComputeTransport {
public:
    void setRuntime(ModelComputeRuntime* updatedRuntime) {
        modelRuntime = updatedRuntime;
    }

    void setECSRegistry(ECSRegistry* updatedRegistry) {
        ecsRegistry = updatedRegistry;
    }

    void sync(const ECSRegistry& registry);
    void finalizeSync();

private:
    bool tryBuildRuntimeModelPath(uint64_t socketKey, const ModelPackage& package, const std::string*& outModelPath) const;
    void removePublishedProduct(uint64_t socketKey);
    void publishProduct(uint64_t socketKey, uint32_t runtimeModelId);

    ModelComputeRuntime* modelRuntime = nullptr;
    ECSRegistry* ecsRegistry = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::unordered_map<uint64_t, uint64_t> appliedPackageHash;
};
