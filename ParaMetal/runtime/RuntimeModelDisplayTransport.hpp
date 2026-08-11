#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/RuntimeProductManager.hpp"

class ModelDisplayController;

class RuntimeModelDisplayTransport {
public:
    void setController(ModelDisplayController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    void sync(const RuntimePackageManager& registry, const std::unordered_set<uint64_t>& visibleKeys);
    void finalizeSync();

private:
    ModelDisplayController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
    std::unordered_set<uint64_t> activeSocketKeys;
};
