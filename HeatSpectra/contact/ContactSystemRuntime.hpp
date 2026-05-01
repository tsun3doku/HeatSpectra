#pragma once

#include "contact/ContactTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <vector>

class MemoryAllocator;
class VulkanDevice;

class ContactSystemRuntime {
public:
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
    bool buildCoupling(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);

    void clear(MemoryAllocator& memoryAllocator);
    bool hasValidBinding() const;
    const ContactCoupling* getContactCoupling() const { return couplingValid ? &coupling : nullptr; }
    VkBuffer getContactPairBuffer() const { return contactPairBuffer; }
    VkDeviceSize getContactPairBufferOffset() const { return contactPairBufferOffset; }
    const std::vector<ContactLineVertex>& getOutlineVertices() const { return outlineVertices; }
    const std::vector<ContactLineVertex>& getCorrespondenceVertices() const { return correspondenceVertices; }
    bool hasContact() const { return hasContactFlag; }

private:
    void clearPairBuffer(MemoryAllocator& memoryAllocator);
    bool recreateContactPairBuffer(
        MemoryAllocator& memoryAllocator,
        VulkanDevice& vulkanDevice,
        VkBuffer& buffer,
        VkDeviceSize& offset,
        void** mappedData,
        const void* data,
        VkDeviceSize size);
    void clearComputedState(MemoryAllocator& memoryAllocator);
    bool computeContactPairs(std::vector<ContactPair>& outPairs);

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
    ContactCoupling coupling{};
    VkBuffer contactPairBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactPairBufferOffset = 0;
    bool couplingValid = false;
    bool bindingDirty = false;

    std::vector<ContactLineVertex> outlineVertices;
    std::vector<ContactLineVertex> correspondenceVertices;
    bool hasContactFlag = false;
};
