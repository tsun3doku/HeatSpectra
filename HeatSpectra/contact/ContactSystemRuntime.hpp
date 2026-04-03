#pragma once

#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <vector>

class ContactSystemController;
class MemoryAllocator;
class VulkanDevice;

class ContactSystemRuntime {
public:
    const ContactProduct* getProduct() const { return productValid ? &product : nullptr; }

    void clear(MemoryAllocator& memoryAllocator);
    void rebuildProducts(
        ContactSystemController* contactSystemController,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const RuntimeContactBinding& configuredContact);

private:
    ContactProduct product{};
    bool productValid = false;
};
