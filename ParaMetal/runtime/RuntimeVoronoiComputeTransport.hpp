#pragma once

#include "voronoi/VoronoiSystemComputeController.hpp"
#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProductManager.hpp"

class RuntimeVoronoiComputeTransport {
public:
    void setController(VoronoiSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    ProductHandle apply(uint64_t socketKey, const VoronoiPackage& package);
    void remove(uint64_t socketKey);

private:
    VoronoiSystemComputeController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
};
