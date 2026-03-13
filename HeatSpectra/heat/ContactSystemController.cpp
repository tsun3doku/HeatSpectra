#include "ContactSystemController.hpp"

#include "mesh/remesher/Remesher.hpp"
#include "renderers/ContactLineRenderer.hpp"
#include "scene/ModelRegistry.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <glm/glm.hpp>

ContactSystemController::ContactSystemController(
    ModelRegistry& modelRegistryRef,
    VulkanDevice& vulkanDeviceRef,
    MemoryAllocator& memoryAllocatorRef,
    ResourceManager& resourceManager,
    Remesher& remesher,
    UniformBufferManager& uniformBufferManagerRef,
    CommandPool& renderCommandPool)
    : modelRegistry(modelRegistryRef),
      vulkanDevice(vulkanDeviceRef),
      memoryAllocator(memoryAllocatorRef),
      uniformBufferManager(uniformBufferManagerRef),
      contactSystem(resourceManager, remesher) {
    (void)renderCommandPool;
}

ContactSystemController::~ContactSystemController() {
    clearRenderer();
}

void ContactSystemController::beginPreviewFrame() {
    previewResultsByNodeId.clear();
}

void ContactSystemController::endPreviewFrame() {
    rebuildPreviewBuffers();
}

bool ContactSystemController::updatePreviewForNodeModels(
    uint32_t ownerNodeId,
    ContactCouplingKind kind,
    uint32_t emitterNodeModelId,
    uint32_t receiverNodeModelId,
    float minNormalDot,
    float contactRadius,
    std::vector<ContactPairGPU>& outPairs,
    bool forceRebuild) {
    outPairs.clear();
    if (ownerNodeId == 0 || emitterNodeModelId == 0 || receiverNodeModelId == 0 || emitterNodeModelId == receiverNodeModelId) {
        return false;
    }

    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
    if (!modelRegistry.tryGetNodeModelRuntimeId(emitterNodeModelId, emitterRuntimeModelId) ||
        !modelRegistry.tryGetNodeModelRuntimeId(receiverNodeModelId, receiverRuntimeModelId) ||
        emitterRuntimeModelId == 0 ||
        receiverRuntimeModelId == 0 ||
        emitterRuntimeModelId == receiverRuntimeModelId) {
        return false;
    }

    ConfiguredContactPair pair{};
    pair.kind = kind;
    pair.emitterModelId = emitterRuntimeModelId;
    pair.receiverModelId = receiverRuntimeModelId;
    pair.minNormalDot = minNormalDot;
    pair.contactRadius = contactRadius;

    ContactSystem::Result result{};
    if (!contactSystem.compute(pair, result, forceRebuild)) {
        return false;
    }

    outPairs = result.pairs;
    previewResultsByNodeId[ownerNodeId] = std::move(result);
    return true;
}

bool ContactSystemController::computePairsForRuntimeModels(
    const ConfiguredContactPair& pair,
    std::vector<ContactPairGPU>& outPairs,
    bool forceRebuild) {
    outPairs.clear();

    ContactSystem::Result result{};
    if (!contactSystem.compute(pair, result, forceRebuild)) {
        return false;
    }

    outPairs = std::move(result.pairs);
    return true;
}

void ContactSystemController::initRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (!contactLineRenderer) {
        contactLineRenderer = std::make_unique<ContactLineRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager);
    }

    if (contactLineRenderer) {
        contactLineRenderer->initialize(renderPass, 2, maxFramesInFlight);
        rebuildPreviewBuffers();
    }
}

void ContactSystemController::reinitRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    clearRenderer();
    initRenderer(renderPass, maxFramesInFlight);
}

void ContactSystemController::clearRenderer() {
    if (contactLineRenderer) {
        contactLineRenderer->cleanup();
        contactLineRenderer.reset();
    }
}

void ContactSystemController::renderLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) const {
    if (!contactLineRenderer) {
        return;
    }

    contactLineRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void ContactSystemController::clearCache() {
    contactSystem.clearCache();
    previewResultsByNodeId.clear();
    rebuildPreviewBuffers();
}

void ContactSystemController::rebuildPreviewBuffers() {
    if (!contactLineRenderer) {
        return;
    }

    std::vector<ContactLineRenderer::LineVertex> outlineVertices;
    std::vector<ContactLineRenderer::LineVertex> correspondenceVertices;

    for (const auto& entry : previewResultsByNodeId) {
        const ContactSystem::Result& result = entry.second;
        for (const ContactInterface::ContactLineVertex& sourceVertex : result.outlineVertices) {
            ContactLineRenderer::LineVertex vertex{};
            vertex.position = sourceVertex.position;
            vertex.color = sourceVertex.color;
            outlineVertices.push_back(vertex);
        }

        for (const ContactInterface::ContactLineVertex& sourceVertex : result.correspondenceVertices) {
            ContactLineRenderer::LineVertex vertex{};
            vertex.position = sourceVertex.position;
            vertex.color = sourceVertex.color;
            correspondenceVertices.push_back(vertex);
        }
    }

    contactLineRenderer->uploadOutlines(outlineVertices);
    contactLineRenderer->uploadCorrespondences(correspondenceVertices);
}
