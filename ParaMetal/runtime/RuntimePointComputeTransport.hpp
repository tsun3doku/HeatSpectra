#pragma once

#include <cstdint>

#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/PointComputeRuntime.hpp"

class RuntimePointComputeTransport {
public:
    void setRuntime(PointComputeRuntime* updatedRuntime) {
        runtime = updatedRuntime;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    ProductHandle apply(uint64_t socketKey, const PointPackage& package);
    void remove(uint64_t socketKey);

private:
    PointComputeRuntime* runtime = nullptr;
    RuntimeProductManager* products = nullptr;
};
