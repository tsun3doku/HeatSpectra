#include "ContactSystemRuntime.hpp"

#include "ContactSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <vector>

void ContactSystemRuntime::setParams(
    ContactCouplingType updatedCouplingType,
    float updatedMinNormalDot,
    float updatedContactRadius) {
    couplingType = updatedCouplingType;
    minNormalDot = updatedMinNormalDot;
    contactRadius = updatedContactRadius;
    bindingDirty = true;
}

void ContactSystemRuntime::setEmitterState(
    uint32_t modelId,
    const std::array<float, 16>& localToWorld,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
    uint32_t runtimeModelId) {
    emitterModelId = modelId;
    emitterLocalToWorld = localToWorld;
    emitterIntrinsicMesh = intrinsicMesh;
    emitterRuntimeModelId = runtimeModelId;
    bindingDirty = true;
}

void ContactSystemRuntime::setReceiverState(
    uint32_t modelId,
    const std::array<float, 16>& localToWorld,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
    uint32_t runtimeModelId) {
    receiverModelId = modelId;
    receiverLocalToWorld = localToWorld;
    receiverIntrinsicMesh = intrinsicMesh;
    receiverRuntimeModelId = runtimeModelId;
    bindingDirty = true;
}

void ContactSystemRuntime::setReceiverTriangleIndices(const std::vector<uint32_t>& triangleIndices) {
    receiverTriangleIndices = triangleIndices;
    bindingDirty = true;
}

void ContactSystemRuntime::clearProductBuffers(MemoryAllocator& memoryAllocator) {
    if (product.contactPairBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(product.contactPairBuffer, product.contactPairBufferOffset);
        product.contactPairBuffer = VK_NULL_HANDLE;
        product.contactPairBufferOffset = 0;
    }
    product.contactPairCount = 0;
    product.mappedContactPairs = nullptr;
}

bool ContactSystemRuntime::recreateProductBuffer(
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

void ContactSystemRuntime::clearProduct(MemoryAllocator& memoryAllocator) {
    clearProductBuffers(memoryAllocator);
    product = {};
    productValid = false;
}

void ContactSystemRuntime::clear(MemoryAllocator& memoryAllocator) {
    clearProduct(memoryAllocator);
    couplingType = ContactCouplingType::SourceToReceiver;
    minNormalDot = -0.65f;
    contactRadius = 0.01f;
    emitterModelId = 0;
    emitterLocalToWorld = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    emitterIntrinsicMesh = {};
    emitterRuntimeModelId = 0;
    receiverModelId = 0;
    receiverLocalToWorld = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    receiverIntrinsicMesh = {};
    receiverRuntimeModelId = 0;
    receiverTriangleIndices.clear();
    bindingDirty = false;
}

bool ContactSystemRuntime::hasValidBinding() const {
    return emitterRuntimeModelId != 0 &&
        receiverRuntimeModelId != 0 &&
        emitterRuntimeModelId != receiverRuntimeModelId &&
        emitterModelId != 0 &&
        receiverModelId != 0 &&
        !emitterIntrinsicMesh.vertices.empty() &&
        !receiverIntrinsicMesh.vertices.empty() &&
        !receiverTriangleIndices.empty();
}

bool ContactSystemRuntime::ensureProduct(
    ContactSystem& contactSystem,
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator) {
    if (!bindingDirty) {
        return productValid;
    }

    clearProduct(memoryAllocator);

    if (!hasValidBinding()) {
        return false;
    }

    std::vector<ContactPair> pairs;
    if (!contactSystem.computePairs(
            emitterModelId,
            emitterLocalToWorld,
            emitterIntrinsicMesh,
            receiverModelId,
            receiverLocalToWorld,
            receiverIntrinsicMesh,
            couplingType,
            minNormalDot,
            contactRadius,
            pairs) ||
        pairs.empty()) {
        return false;
    }

    product.couplingType = couplingType;
    product.emitterRuntimeModelId = emitterRuntimeModelId;
    product.receiverRuntimeModelId = receiverRuntimeModelId;
    product.receiverTriangleIndices = receiverTriangleIndices;
    if (product.receiverTriangleIndices.empty()) {
        clearProduct(memoryAllocator);
        return false;
    }

    void* mappedPairData = nullptr;
    if (!recreateProductBuffer(
            memoryAllocator,
            vulkanDevice,
            product.contactPairBuffer,
            product.contactPairBufferOffset,
            &mappedPairData,
            pairs.data(),
            sizeof(ContactPair) * pairs.size())) {
        clearProduct(memoryAllocator);
        return false;
    }

    product.contactPairCount = static_cast<uint32_t>(pairs.size());
    product.mappedContactPairs = static_cast<const ContactPair*>(mappedPairData);
    productValid = product.isValid();
    if (productValid) {
        bindingDirty = false;
    }
    return productValid;
}
