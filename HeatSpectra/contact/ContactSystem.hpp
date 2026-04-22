#pragma once

#include "contact/ContactTypes.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class ContactSystemRuntime;
class MemoryAllocator;
class VulkanDevice;

class ContactSystem {
public:
    ContactSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator);
    ~ContactSystem();

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
    void ensureConfigured();
    void disable();

    const ContactCoupling* getContactCoupling() const;
    VkBuffer getContactPairBuffer() const;
    VkDeviceSize getContactPairBufferOffset() const;
    const std::vector<ContactLineVertex>& getOutlineVertices() const;
    const std::vector<ContactLineVertex>& getCorrespondenceVertices() const;
    bool hasContact() const;

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    std::unique_ptr<ContactSystemRuntime> runtime;
};