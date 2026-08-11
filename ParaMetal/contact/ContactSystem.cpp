#include "ContactSystem.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

ContactSystem::ContactSystem(
    VulkanDevice& vulkanDeviceRef,
    MemoryAllocator& memoryAllocatorRef,
    CommandPool& commandPoolRef)
    : vulkanDevice(vulkanDeviceRef),
      memoryAllocator(memoryAllocatorRef),
      commandPool(commandPoolRef) {
}

ContactSystem::~ContactSystem() {
    disable();
}

void ContactSystem::setParams(float minNormalDot, float contactRadius) {
    this->minNormalDot = minNormalDot;
    this->contactRadius = contactRadius;
    bindingDirty = true;
}

void ContactSystem::setModelAState(
    const std::array<float, 16>& localToWorld,
    const ContactMesh& mesh,
    uint32_t runtimeModelId) {
    modelALocalToWorld = localToWorld;
    modelAMesh = mesh;
    modelARuntimeModelId = runtimeModelId;
    bindingDirty = true;
}

void ContactSystem::setModelBState(
    const std::array<float, 16>& localToWorld,
    const ContactMesh& mesh,
    uint32_t runtimeModelId) {
    modelBLocalToWorld = localToWorld;
    modelBMesh = mesh;
    modelBRuntimeModelId = runtimeModelId;
    bindingDirty = true;
}

void ContactSystem::ensureConfigured() {
    if (!bindingDirty) {
        return;
    }
    rebuildCoupling();
}

void ContactSystem::disable() {
    couplingRuntime.clear();
    minNormalDot = 0.0f;
    contactRadius = 0.0f;
    modelALocalToWorld = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    modelAMesh = {};
    modelARuntimeModelId = 0;
    modelBLocalToWorld = modelALocalToWorld;
    modelBMesh = {};
    modelBRuntimeModelId = 0;
    bindingDirty = false;
}

const ContactCoupling* ContactSystem::getContactCoupling() const {
    return couplingRuntime.getContactCoupling();
}

VkBuffer ContactSystem::getContactPairBuffer() const {
    return couplingRuntime.getContactPairBuffer();
}

VkDeviceSize ContactSystem::getContactPairBufferOffset() const {
    return couplingRuntime.getContactPairBufferOffset();
}

const std::vector<ContactLineVertex>& ContactSystem::getOutlineVertices() const {
    return couplingRuntime.getOutlineVertices();
}

const std::vector<ContactLineVertex>& ContactSystem::getCorrespondenceVertices() const {
    return couplingRuntime.getCorrespondenceVertices();
}

bool ContactSystem::hasValidBinding() const {
    return modelARuntimeModelId != 0 &&
        modelBRuntimeModelId != 0 &&
        modelARuntimeModelId != modelBRuntimeModelId &&
        modelAMesh.isValid() &&
        modelBMesh.isValid();
}

bool ContactSystem::hasUsableContactPairs(const std::vector<ContactPair>& pairs) const {
    for (const ContactPair& pair : pairs) {
        if (pair.contactArea > 0.0f) {
            return true;
        }
    }
    return false;
}

bool ContactSystem::computeContactPairs(
    std::vector<ContactPair>& pairs,
    std::vector<ContactLineVertex>& outlineVertices,
    std::vector<ContactLineVertex>& correspondenceVertices) const {
    pairs.clear();
    outlineVertices.clear();
    correspondenceVertices.clear();
    if (!hasValidBinding()) {
        return false;
    }

    buildContactPairs(
        modelAMesh,
        modelALocalToWorld,
        modelBMesh,
        modelBLocalToWorld,
        pairs,
        outlineVertices,
        correspondenceVertices,
        contactRadius,
        minNormalDot);
    return hasUsableContactPairs(pairs);
}

bool ContactSystem::recreateContactPairBuffer(
    VkBuffer& buffer,
    VkDeviceSize& offset,
    const void* data,
    VkDeviceSize size) {
    buffer = VK_NULL_HANDLE;
    offset = 0;
    if (data == nullptr || size == 0) {
        return false;
    }

    const VkDeviceSize alignment =
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    return uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        data,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        alignment,
        buffer,
        offset) == VK_SUCCESS && buffer != VK_NULL_HANDLE;
}

bool ContactSystem::rebuildCoupling() {
    couplingRuntime.clear();
    if (!hasValidBinding()) {
        return false;
    }

    std::vector<ContactPair> pairs;
    std::vector<ContactLineVertex> outlineVertices;
    std::vector<ContactLineVertex> correspondenceVertices;
    if (!computeContactPairs(pairs, outlineVertices, correspondenceVertices) || pairs.empty()) {
        return false;
    }

    ContactCoupling coupling{};
    coupling.modelARuntimeModelId = modelARuntimeModelId;
    coupling.modelBRuntimeModelId = modelBRuntimeModelId;
    coupling.modelBTriangleIndices = modelBMesh.indices;
    if (coupling.modelBTriangleIndices.empty()) {
        return false;
    }

    VkBuffer contactPairBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactPairBufferOffset = 0;
    if (!recreateContactPairBuffer(
            contactPairBuffer,
            contactPairBufferOffset,
            pairs.data(),
            sizeof(ContactPair) * pairs.size())) {
        return false;
    }

    coupling.contactPairCount = static_cast<uint32_t>(pairs.size());
    coupling.contactPairs = std::move(pairs);
    if (!coupling.isValid()) {
        return false;
    }

    couplingRuntime.setCoupling(
        std::move(coupling),
        contactPairBuffer,
        contactPairBufferOffset,
        std::move(outlineVertices),
        std::move(correspondenceVertices));
    bindingDirty = false;
    return true;
}
