#include "ContactSystemRuntime.hpp"

#include "contact/ContactSampling.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <vector>

namespace {

bool hasUsableContactPairs(const std::vector<ContactPair>& pairs) {
    for (const ContactPair& pair : pairs) {
        if (pair.contactArea > 0.0f) {
            return true;
        }
    }

    return false;
}

}

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

void ContactSystemRuntime::clearPairBuffer(MemoryAllocator& memoryAllocator) {
    if (contactPairBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(contactPairBuffer, contactPairBufferOffset);
        contactPairBuffer = VK_NULL_HANDLE;
        contactPairBufferOffset = 0;
    }
    coupling.contactPairCount = 0;
    coupling.mappedContactPairs = nullptr;
}

bool ContactSystemRuntime::recreateContactPairBuffer(
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

void ContactSystemRuntime::clearComputedState(MemoryAllocator& memoryAllocator) {
    clearPairBuffer(memoryAllocator);
    coupling = {};
    couplingValid = false;
    outlineVertices.clear();
    correspondenceVertices.clear();
    hasContactFlag = false;
}

void ContactSystemRuntime::clear(MemoryAllocator& memoryAllocator) {
    clearComputedState(memoryAllocator);
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

bool ContactSystemRuntime::computeContactPairs(std::vector<ContactPair>& outPairs) {
    outPairs.clear();
    if (emitterModelId == 0 ||
        receiverModelId == 0 ||
        emitterIntrinsicMesh.vertices.empty() ||
        receiverIntrinsicMesh.vertices.empty()) {
        return false;
    }

    std::vector<std::vector<ContactPair>> receiverContactPairs;
    std::vector<const SupportingHalfedge::IntrinsicMesh*> receiverIntrinsicMeshes;
    std::vector<std::array<float, 16>> receiverLocalToWorlds;
    receiverIntrinsicMeshes.push_back(&receiverIntrinsicMesh);
    receiverLocalToWorlds.push_back(receiverLocalToWorld);

    std::vector<ContactLineVertex> computedOutlineVertices;
    std::vector<ContactLineVertex> computedCorrespondenceVertices;

    mapSurfacePoints(
        emitterIntrinsicMesh,
        emitterLocalToWorld,
        receiverIntrinsicMeshes,
        receiverLocalToWorlds,
        receiverContactPairs,
        computedOutlineVertices,
        computedCorrespondenceVertices,
        contactRadius,
        minNormalDot);

    outlineVertices = std::move(computedOutlineVertices);
    correspondenceVertices = std::move(computedCorrespondenceVertices);

    if (!receiverContactPairs.empty()) {
        outPairs = std::move(receiverContactPairs.front());
    }
    return hasUsableContactPairs(outPairs);
}

bool ContactSystemRuntime::buildCoupling(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator) {
    clearComputedState(memoryAllocator);

    if (!hasValidBinding()) {
        return false;
    }

    std::vector<ContactPair> pairs;
    if (!computeContactPairs(pairs) || pairs.empty()) {
        return false;
    }

    coupling.couplingType = couplingType;
    coupling.emitterRuntimeModelId = emitterRuntimeModelId;
    coupling.receiverRuntimeModelId = receiverRuntimeModelId;
    coupling.receiverTriangleIndices = receiverTriangleIndices;
    if (coupling.receiverTriangleIndices.empty()) {
        clearComputedState(memoryAllocator);
        return false;
    }

    void* mappedPairData = nullptr;
    if (!recreateContactPairBuffer(
            memoryAllocator,
            vulkanDevice,
            contactPairBuffer,
            contactPairBufferOffset,
            &mappedPairData,
            pairs.data(),
            sizeof(ContactPair) * pairs.size())) {
        clearComputedState(memoryAllocator);
        return false;
    }

    coupling.contactPairCount = static_cast<uint32_t>(pairs.size());
    coupling.mappedContactPairs = static_cast<const ContactPair*>(mappedPairData);
    couplingValid = coupling.isValid();
    hasContactFlag = couplingValid;
    if (couplingValid) {
        bindingDirty = false;
    }
    return couplingValid;
}
