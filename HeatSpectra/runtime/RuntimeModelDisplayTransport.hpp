#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "runtime/RuntimeECS.hpp"
#include "runtime/RuntimePackages.hpp"

class ModelDisplayRuntime;

class RuntimeModelDisplayTransport {
public:
    void setRuntime(ModelDisplayRuntime* updatedRuntime) {
        modelRuntime = updatedRuntime;
    }

    void setVisibleKeys(const std::unordered_set<uint64_t>* keys) {
        visibleKeys = keys;
    }

    void sync(const ECSRegistry& registry);
    void finalizeSync();

private:
    ModelDisplayRuntime* modelRuntime = nullptr;
    const std::unordered_set<uint64_t>* visibleKeys = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
