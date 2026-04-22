#include "ContactSystem.hpp"

#include "ContactSystemRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

ContactSystem::ContactSystem(
    VulkanDevice& vulkanDeviceRef,
    MemoryAllocator& memoryAllocatorRef)
    : vulkanDevice(vulkanDeviceRef),
      memoryAllocator(memoryAllocatorRef),
      runtime(std::make_unique<ContactSystemRuntime>()) {
}

ContactSystem::~ContactSystem() {
    disable();
}

void ContactSystem::setParams(
    ContactCouplingType couplingType,
    float minNormalDot,
    float contactRadius) {
    if (!runtime) {
        return;
    }

    runtime->setParams(couplingType, minNormalDot, contactRadius);
}

void ContactSystem::setEmitterState(
    uint32_t modelId,
    const std::array<float, 16>& localToWorld,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
    uint32_t runtimeModelId) {
    if (!runtime) {
        return;
    }

    runtime->setEmitterState(modelId, localToWorld, intrinsicMesh, runtimeModelId);
}

void ContactSystem::setReceiverState(
    uint32_t modelId,
    const std::array<float, 16>& localToWorld,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
    uint32_t runtimeModelId) {
    if (!runtime) {
        return;
    }

    runtime->setReceiverState(modelId, localToWorld, intrinsicMesh, runtimeModelId);
}

void ContactSystem::setReceiverTriangleIndices(const std::vector<uint32_t>& triangleIndices) {
    if (!runtime) {
        return;
    }

    runtime->setReceiverTriangleIndices(triangleIndices);
}

void ContactSystem::ensureConfigured() {
    if (!runtime || !runtime->needsRebuild()) {
        return;
    }

    runtime->buildCoupling(vulkanDevice, memoryAllocator);
}

void ContactSystem::disable() {
    if (runtime) {
        runtime->clear(memoryAllocator);
    }
}

const ContactCoupling* ContactSystem::getContactCoupling() const {
    return runtime ? runtime->getContactCoupling() : nullptr;
}

VkBuffer ContactSystem::getContactPairBuffer() const {
    return runtime ? runtime->getContactPairBuffer() : VK_NULL_HANDLE;
}

VkDeviceSize ContactSystem::getContactPairBufferOffset() const {
    return runtime ? runtime->getContactPairBufferOffset() : 0;
}

const std::vector<ContactLineVertex>& ContactSystem::getOutlineVertices() const {
    static const std::vector<ContactLineVertex> empty;
    return runtime ? runtime->getOutlineVertices() : empty;
}

const std::vector<ContactLineVertex>& ContactSystem::getCorrespondenceVertices() const {
    static const std::vector<ContactLineVertex> empty;
    return runtime ? runtime->getCorrespondenceVertices() : empty;
}

bool ContactSystem::hasContact() const {
    return runtime ? runtime->hasContact() : false;
}