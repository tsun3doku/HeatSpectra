#pragma once

#include "contact/ContactTypes.hpp"
#include "contact/ContactMapping.hpp"
#include "contact/ContactCouplingRuntime.hpp"
#include "vulkan/CommandBufferManager.hpp"

#include <array>
#include <cstdint>
#include <vector>

class MemoryAllocator;
class VulkanDevice;

class ContactSystem {
public:
    ContactSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~ContactSystem();

    void setParams(float minNormalDot, float contactRadius);
    void setModelAState(
        const std::array<float, 16>& localToWorld,
        const ContactMesh& mesh,
        uint32_t runtimeModelId);
    void setModelBState(
        const std::array<float, 16>& localToWorld,
        const ContactMesh& mesh,
        uint32_t runtimeModelId);
    void ensureConfigured();
    void disable();

    const ContactCoupling* getContactCoupling() const;
    VkBuffer getContactPairBuffer() const;
    VkDeviceSize getContactPairBufferOffset() const;
    const std::vector<ContactLineVertex>& getOutlineVertices() const;
    const std::vector<ContactLineVertex>& getCorrespondenceVertices() const;

private:
    bool hasValidBinding() const;
    bool hasUsableContactPairs(const std::vector<ContactPair>& pairs) const;
    bool computeContactPairs(
        std::vector<ContactPair>& pairs,
        std::vector<ContactLineVertex>& outlineVertices,
        std::vector<ContactLineVertex>& correspondenceVertices) const;
    bool recreateContactPairBuffer(
        VkBuffer& buffer,
        VkDeviceSize& offset,
        const void* data,
        VkDeviceSize size);
    bool rebuildCoupling();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;
    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
    std::array<float, 16> modelALocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    ContactMesh modelAMesh;
    uint32_t modelARuntimeModelId = 0;
    std::array<float, 16> modelBLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    ContactMesh modelBMesh;
    uint32_t modelBRuntimeModelId = 0;
    bool bindingDirty = false;
    ContactCouplingRuntime couplingRuntime;
};
