#pragma once

#include <cstdint>
#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProductManager.hpp"

class ModelComputeController;

class RuntimeModelComputeTransport {
public:
    void setController(ModelComputeController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    ProductHandle apply(uint64_t socketKey, const ModelPackage& package);
    void remove(uint64_t socketKey);

private:
    ModelComputeController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
};
