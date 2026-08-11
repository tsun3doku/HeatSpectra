#pragma once


#include "contact/ContactSystem.hpp"
#include "contact/ContactSystemComputeController.hpp"
#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProductManager.hpp"

class RuntimeContactComputeTransport {
public:
    void setController(ContactSystemComputeController* updatedController) {
        controller = updatedController;
    }

    void setProducts(RuntimeProductManager* updatedProducts) {
        products = updatedProducts;
    }

    ProductHandle apply(uint64_t socketKey, const ContactPackage& package);
    void remove(uint64_t socketKey);

private:
    ContactSystemComputeController* controller = nullptr;
    RuntimeProductManager* products = nullptr;
};
