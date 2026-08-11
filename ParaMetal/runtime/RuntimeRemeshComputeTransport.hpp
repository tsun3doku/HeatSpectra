#pragma once

#include "runtime/RemeshController.hpp"
#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProductManager.hpp"

class RuntimeRemeshComputeTransport {
public:
    void setController(RemeshController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    ProductHandle apply(uint64_t socketKey, const RemeshPackage& package);
    void remove(uint64_t socketKey);

private:
    RemeshController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
};
