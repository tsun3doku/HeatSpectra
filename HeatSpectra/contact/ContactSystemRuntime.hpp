#pragma once

#include "contact/ContactSystemComputeController.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <vector>

class ContactSystem;
class MemoryAllocator;
class VulkanDevice;

class ContactSystemRuntime {
public:
    const ContactProduct* getProduct() const { return productValid ? &product : nullptr; }
    bool needsRebuild() const { return bindingDirty; }

    void setParams(
        ContactCouplingType couplingType,
        float minNormalDot,
        float contactRadius);
    void setEmitterState(
        uint32_t modelId,
        const std::array<float, 16>& localToWorld,
        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
        uint32_t runtimeModelId);
    void setReceiverState(
        uint32_t modelId,
        const std::array<float, 16>& localToWorld,
        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
        uint32_t runtimeModelId);
    void setReceiverTriangleIndices(const std::vector<uint32_t>& triangleIndices);
    bool ensureProduct(
        ContactSystem& contactSystem,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator);

    void clear(MemoryAllocator& memoryAllocator);
    bool hasValidBinding() const;
    ContactCouplingType getCouplingType() const { return couplingType; }
    float getMinNormalDot() const { return minNormalDot; }
    float getContactRadius() const { return contactRadius; }
    uint32_t getEmitterModelId() const { return emitterModelId; }
    const std::array<float, 16>& getEmitterLocalToWorld() const { return emitterLocalToWorld; }
    const SupportingHalfedge::IntrinsicMesh& getEmitterIntrinsicMesh() const { return emitterIntrinsicMesh; }
    uint32_t getEmitterRuntimeModelId() const { return emitterRuntimeModelId; }
    uint32_t getReceiverModelId() const { return receiverModelId; }
    const std::array<float, 16>& getReceiverLocalToWorld() const { return receiverLocalToWorld; }
    const SupportingHalfedge::IntrinsicMesh& getReceiverIntrinsicMesh() const { return receiverIntrinsicMesh; }
    uint32_t getReceiverRuntimeModelId() const { return receiverRuntimeModelId; }
    const std::vector<uint32_t>& getReceiverTriangleIndices() const { return receiverTriangleIndices; }

private:
    void clearProductBuffers(MemoryAllocator& memoryAllocator);
    bool recreateProductBuffer(
        MemoryAllocator& memoryAllocator,
        VulkanDevice& vulkanDevice,
        VkBuffer& buffer,
        VkDeviceSize& offset,
        void** mappedData,
        const void* data,
        VkDeviceSize size);
    void clearProduct(MemoryAllocator& memoryAllocator);

    ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
    uint32_t emitterModelId = 0;
    std::array<float, 16> emitterLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    SupportingHalfedge::IntrinsicMesh emitterIntrinsicMesh;
    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverModelId = 0;
    std::array<float, 16> receiverLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    SupportingHalfedge::IntrinsicMesh receiverIntrinsicMesh;
    uint32_t receiverRuntimeModelId = 0;
    std::vector<uint32_t> receiverTriangleIndices;
    ContactProduct product{};
    bool productValid = false;
    bool bindingDirty = false;
};
