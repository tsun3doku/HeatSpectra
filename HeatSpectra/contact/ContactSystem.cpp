#include "ContactSystem.hpp"

#include "ContactSystemRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"

#include <glm/glm.hpp>

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

ContactSystem::ContactSystem(
    VulkanDevice& vulkanDeviceRef,
    MemoryAllocator& memoryAllocatorRef,
    UniformBufferManager& uniformBufferManagerRef,
    uint32_t maxFramesInFlight,
    VkRenderPass renderPass)
    : vulkanDevice(vulkanDeviceRef),
      memoryAllocator(memoryAllocatorRef),
      uniformBufferManager(uniformBufferManagerRef),
      runtime(std::make_unique<ContactSystemRuntime>()) {
    updateRenderResources(maxFramesInFlight, renderPass);
}

ContactSystem::~ContactSystem() {
    disable();
    clearRenderer();
}

void ContactSystem::updateRenderResources(uint32_t maxFramesInFlight, VkRenderPass renderPass) {
    clearRenderer();
    contactLineRenderer = std::make_unique<ContactLineRenderer>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager);
    if (!contactLineRenderer) {
        initialized = false;
        return;
    }

    contactLineRenderer->initialize(renderPass, 2, maxFramesInFlight);
    initialized = true;
    previewDirty = true;
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

    runtime->ensureProduct(
        *this,
        vulkanDevice,
        memoryAllocator);
}

void ContactSystem::disable() {
    clearPreview();
    if (runtime) {
        runtime->clear(memoryAllocator);
    }
}

bool ContactSystem::exportProduct(ContactProduct& outProduct) const {
    outProduct = {};
    if (!runtime) {
        return false;
    }

    const ContactProduct* product = runtime->getProduct();
    if (!product) {
        return false;
    }

    outProduct = *product;
    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

static bool computeContactPairs(
    ContactInterface& contactInterface,
    uint32_t emitterModelId,
    const std::array<float, 16>& emitterLocalToWorld,
    const SupportingHalfedge::IntrinsicMesh& emitterIntrinsicMesh,
    uint32_t receiverModelId,
    const std::array<float, 16>& receiverLocalToWorld,
    const SupportingHalfedge::IntrinsicMesh& receiverIntrinsicMesh,
    ContactCouplingType couplingType,
    float minNormalDot,
    float contactRadius,
    ContactSystem::Result& outResult) {
    (void)couplingType;
    outResult = {};
    if (emitterModelId == 0 ||
        receiverModelId == 0 ||
        emitterModelId == receiverModelId ||
        emitterIntrinsicMesh.vertices.empty() ||
        receiverIntrinsicMesh.vertices.empty()) {
        return false;
    }

    ContactInterface::Settings settings{};
    settings.minNormalDot = minNormalDot;
    settings.contactRadius = contactRadius;

    std::vector<std::vector<ContactPair>> receiverContactPairs;
    std::vector<const SupportingHalfedge::IntrinsicMesh*> receiverIntrinsicMeshes;
    std::vector<std::array<float, 16>> receiverLocalToWorlds;
    receiverIntrinsicMeshes.push_back(&receiverIntrinsicMesh);
    receiverLocalToWorlds.push_back(receiverLocalToWorld);

    contactInterface.mapSurfacePoints(
        emitterIntrinsicMesh,
        emitterLocalToWorld,
        receiverIntrinsicMeshes,
        receiverLocalToWorlds,
        receiverContactPairs,
        outResult.outlineVertices,
        outResult.correspondenceVertices,
        settings);

    if (!receiverContactPairs.empty()) {
        outResult.pairs = receiverContactPairs.front();
    }
    outResult.hasContact = hasUsableContactPairs(outResult.pairs);
    return outResult.hasContact;
}

void ContactSystem::refreshPreview() {
    if (!runtime) {
        clearPreview();
        return;
    }

    if (!runtime->hasValidBinding()) {
        clearPreview();
        return;
    }

    Result freshResult{};
    if (!computeContactPairs(
            contactInterface,
            runtime->getEmitterModelId(),
            runtime->getEmitterLocalToWorld(),
            runtime->getEmitterIntrinsicMesh(),
            runtime->getReceiverModelId(),
            runtime->getReceiverLocalToWorld(),
            runtime->getReceiverIntrinsicMesh(),
            runtime->getCouplingType(),
            runtime->getMinNormalDot(),
            runtime->getContactRadius(),
            freshResult)) {
        clearPreview();
        return;
    }

    previewResult = freshResult;
    previewValid = true;
    previewDirty = true;
}

bool ContactSystem::computePairs(
    uint32_t emitterModelId,
    const std::array<float, 16>& emitterLocalToWorld,
    const SupportingHalfedge::IntrinsicMesh& emitterIntrinsicMesh,
    uint32_t receiverModelId,
    const std::array<float, 16>& receiverLocalToWorld,
    const SupportingHalfedge::IntrinsicMesh& receiverIntrinsicMesh,
    ContactCouplingType couplingType,
    float minNormalDot,
    float contactRadius,
    std::vector<ContactPair>& outPairs) {
    outPairs.clear();
    if (!emitterModelId ||
        !receiverModelId ||
        emitterIntrinsicMesh.vertices.empty() ||
        receiverIntrinsicMesh.vertices.empty()) {
        return false;
    }

    Result freshResult{};
    if (!computeContactPairs(
            contactInterface,
            emitterModelId,
            emitterLocalToWorld,
            emitterIntrinsicMesh,
            receiverModelId,
            receiverLocalToWorld,
            receiverIntrinsicMesh,
            couplingType,
            minNormalDot,
            contactRadius,
            freshResult) ||
        !freshResult.hasContact) {
        return false;
    }

    outPairs = freshResult.pairs;
    return !outPairs.empty();
}

void ContactSystem::clearPreview() {
    if (!previewValid &&
        previewResult.outlineVertices.empty() &&
        previewResult.correspondenceVertices.empty()) {
        return;
    }

    previewResult = {};
    previewValid = false;
    previewDirty = true;
}

void ContactSystem::renderContactLines(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    VkExtent2D extent) {
    if (!contactLineRenderer || !initialized) {
        return;
    }
    if (previewDirty) {
        rebuildPreviewBuffers();
        previewDirty = false;
    }
    contactLineRenderer->render(commandBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void ContactSystem::rebuildPreviewBuffers() {
    if (!contactLineRenderer) {
        return;
    }

    std::vector<ContactLineRenderer::LineVertex> outlineVertices;
    outlineVertices.reserve(previewResult.outlineVertices.size());
    for (const ContactInterface::ContactLineVertex& sourceVertex : previewResult.outlineVertices) {
        ContactLineRenderer::LineVertex vertex{};
        vertex.position = sourceVertex.position;
        vertex.color = sourceVertex.color;
        outlineVertices.push_back(vertex);
    }

    std::vector<ContactLineRenderer::LineVertex> correspondenceVertices;
    correspondenceVertices.reserve(previewResult.correspondenceVertices.size());
    for (const ContactInterface::ContactLineVertex& sourceVertex : previewResult.correspondenceVertices) {
        ContactLineRenderer::LineVertex vertex{};
        vertex.position = sourceVertex.position;
        vertex.color = sourceVertex.color;
        correspondenceVertices.push_back(vertex);
    }

    contactLineRenderer->uploadOutlines(outlineVertices);
    contactLineRenderer->uploadCorrespondences(correspondenceVertices);
}

void ContactSystem::clearRenderer() {
    if (contactLineRenderer) {
        contactLineRenderer->cleanup();
        contactLineRenderer.reset();
    }
    initialized = false;
}
