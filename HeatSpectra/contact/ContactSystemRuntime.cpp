#include "ContactSystemRuntime.hpp"

#include "ContactSystemController.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>
#include <vector>

namespace {

void freeProductBuffers(MemoryAllocator& memoryAllocator, ContactProduct& product) {
    if (product.contactPairBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(product.contactPairBuffer, product.contactPairBufferOffset);
        product.contactPairBuffer = VK_NULL_HANDLE;
        product.contactPairBufferOffset = 0;
    }
    product.contactPairCount = 0;
    product.mappedContactPairs = nullptr;
}

bool recreateBuffer(
    MemoryAllocator& memoryAllocator,
    VulkanDevice& vulkanDevice,
    VkBuffer& buffer,
    VkDeviceSize& offset,
    void** mappedData,
    const void* data,
    VkDeviceSize size) {
    if (buffer != VK_NULL_HANDLE) {
        memoryAllocator.free(buffer, offset);
        buffer = VK_NULL_HANDLE;
        offset = 0;
    }

    if (data == nullptr || size == 0) {
        if (mappedData) {
            *mappedData = nullptr;
        }
        return false;
    }

    return createStorageBuffer(
               memoryAllocator,
               vulkanDevice,
               data,
               size,
               buffer,
               offset,
               mappedData,
               true) == VK_SUCCESS &&
        buffer != VK_NULL_HANDLE;
}

}

void ContactSystemRuntime::clear(MemoryAllocator& memoryAllocator) {
    freeProductBuffers(memoryAllocator, product);
    product = {};
    productValid = false;
}

void ContactSystemRuntime::rebuildProducts(
    ContactSystemController* contactSystemController,
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const RuntimeContactBinding& configuredContact) {
    clear(memoryAllocator);
    if (!contactSystemController) {
        return;
    }

    if (!configuredContact.contactPair.hasValidContact ||
        configuredContact.emitterRuntimeModelId == 0 ||
        configuredContact.receiverRuntimeModelId == 0 ||
        configuredContact.emitterRuntimeModelId == configuredContact.receiverRuntimeModelId) {
        return;
    }

    std::vector<ContactPair> pairs;
    if (!contactSystemController->computePairs(configuredContact.runtimePair, pairs, false) ||
        pairs.empty()) {
        std::cerr << "[ContactRuntime] computePairs FAILED or returned 0 pairs" << std::endl;
        return;
    }

    product.couplingType = configuredContact.contactPair.kind;
    product.emitterRuntimeModelId = configuredContact.emitterRuntimeModelId;
    product.receiverRuntimeModelId = configuredContact.receiverRuntimeModelId;
    product.receiverTriangleIndices = configuredContact.receiverTriangleIndices;
    if (product.receiverTriangleIndices.empty()) {
        std::cerr << "[ContactRuntime] receiverTriangleIndices EMPTY — aborting" << std::endl;
        clear(memoryAllocator);
        return;
    }

    void* mappedPairData = nullptr;
    if (!recreateBuffer(
            memoryAllocator,
            vulkanDevice,
            product.contactPairBuffer,
            product.contactPairBufferOffset,
            &mappedPairData,
            pairs.data(),
            sizeof(ContactPair) * pairs.size())) {
        clear(memoryAllocator);
        return;
    }

    product.contactPairCount = static_cast<uint32_t>(pairs.size());
    product.mappedContactPairs = static_cast<const ContactPair*>(mappedPairData);
    productValid = product.isValid();
    std::cerr << "[ContactRuntime] rebuildProducts done: contactPairCount=" << product.contactPairCount
              << " productValid=" << (productValid ? "true" : "false") << std::endl;
}
