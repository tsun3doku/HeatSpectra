#pragma once

#include "heat/ContactInterface.hpp"
#include "ContactTypes.hpp"
#include "renderers/ContactLineRenderer.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class ContactSystemRuntime;
class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;

struct ContactSystemPreviewResult {
    bool hasContact = false;
    std::vector<ContactPair> pairs;
    std::vector<ContactInterface::ContactLineVertex> outlineVertices;
    std::vector<ContactInterface::ContactLineVertex> correspondenceVertices;
};

class ContactSystem {
public:
    using Result = ContactSystemPreviewResult;

    ContactSystem(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        UniformBufferManager& uniformBufferManager,
        uint32_t maxFramesInFlight,
        VkRenderPass renderPass);
    ~ContactSystem();

    bool isInitialized() const { return initialized; }

    void updateRenderResources(uint32_t maxFramesInFlight, VkRenderPass renderPass);
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

    void refreshPreview();
    bool computePairs(
        uint32_t emitterModelId,
        const std::array<float, 16>& emitterLocalToWorld,
        const SupportingHalfedge::IntrinsicMesh& emitterIntrinsicMesh,
        uint32_t receiverModelId,
        const std::array<float, 16>& receiverLocalToWorld,
        const SupportingHalfedge::IntrinsicMesh& receiverIntrinsicMesh,
        ContactCouplingType couplingType,
        float minNormalDot,
        float contactRadius,
        std::vector<ContactPair>& outPairs);
    void clearPreview();
    void renderContactLines(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent);
    const ContactSystemRuntime* runtimeState() const { return runtime.get(); }
    const Result& previewState() const { return previewResult; }
    bool hasPreviewState() const { return previewValid; }

private:
    void rebuildPreviewBuffers();
    void clearRenderer();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    std::unique_ptr<ContactSystemRuntime> runtime;
    std::unique_ptr<ContactLineRenderer> contactLineRenderer;
    Result previewResult{};
    bool previewValid = false;
    bool previewDirty = false;
    bool initialized = false;

    ContactInterface contactInterface;
};
