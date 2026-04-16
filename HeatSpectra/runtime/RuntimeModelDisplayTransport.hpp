#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/RuntimePackages.hpp"

class ModelDisplayRuntime;

class RuntimeModelDisplayTransport {
public:
    void setRuntime(ModelDisplayRuntime* updatedRuntime) {
        modelRuntime = updatedRuntime;
    }

    void sync(const std::unordered_map<uint64_t, ModelPackage>& packagesBySocket);
    void finalizeSync();

private:
    ModelDisplayRuntime* modelRuntime = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
    std::vector<uint64_t> staleSocketKeys;
};
