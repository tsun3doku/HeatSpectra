#include "ContactPreviewStore.hpp"

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"

#include <glm/glm.hpp>
#include <cstring>

ContactPreviewStore::ContactPreviewStore(VulkanDevice& vulkanDeviceRef, MemoryAllocator& memoryAllocatorRef, UniformBufferManager& uniformBufferManagerRef)
    : vulkanDevice(vulkanDeviceRef),
      memoryAllocator(memoryAllocatorRef),
      uniformBufferManager(uniformBufferManagerRef) {
}

ContactPreviewStore::~ContactPreviewStore() {
    clearRenderer();
}

void ContactPreviewStore::endFrame() {
    if (!contactLineRenderer) {
        return;
    }
    if (previewRevision == lastBuiltRevision) {
        return;
    }
    rebuildPreviewBuffers();
    lastBuiltRevision = previewRevision;
}

void ContactPreviewStore::setPreviewForNode(uint32_t ownerNodeId, const ContactSystem::Result& result) {
    if (ownerNodeId == 0) {
        return;
    }
    const uint64_t newHash = computePreviewHash(result);
    const auto existingIt = previewHashByNodeId.find(ownerNodeId);
    if (existingIt != previewHashByNodeId.end() && existingIt->second == newHash) {
        return;
    }
    previewHashByNodeId[ownerNodeId] = newHash;
    previewResultsByNodeId[ownerNodeId] = result;
    ++previewRevision;
}

void ContactPreviewStore::clearPreviewForNode(uint32_t ownerNodeId) {
    if (ownerNodeId == 0) {
        return;
    }
    if (previewResultsByNodeId.erase(ownerNodeId) > 0) {
        previewHashByNodeId.erase(ownerNodeId);
        ++previewRevision;
    }
}

void ContactPreviewStore::clearAllPreviews() {
    if (previewResultsByNodeId.empty()) {
        return;
    }
    previewResultsByNodeId.clear();
    previewHashByNodeId.clear();
    ++previewRevision;
}

void ContactPreviewStore::initRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    if (!contactLineRenderer) {
        contactLineRenderer = std::make_unique<ContactLineRenderer>(
            vulkanDevice,
            memoryAllocator,
            uniformBufferManager);
    }

    if (contactLineRenderer) {
        contactLineRenderer->initialize(renderPass, 2, maxFramesInFlight);
        rebuildPreviewBuffers();
        lastBuiltRevision = previewRevision;
    }
}

void ContactPreviewStore::reinitRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    clearRenderer();
    initRenderer(renderPass, maxFramesInFlight);
}

void ContactPreviewStore::renderLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) const {
    if (!contactLineRenderer) {
        return;
    }
    contactLineRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void ContactPreviewStore::rebuildPreviewBuffers() {
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

void ContactPreviewStore::clearRenderer() {
    if (contactLineRenderer) {
        contactLineRenderer->cleanup();
        contactLineRenderer.reset();
    }
}

uint64_t ContactPreviewStore::combineFloat(uint64_t h, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    h ^= static_cast<uint64_t>(bits) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

uint64_t ContactPreviewStore::computePreviewHash(const ContactSystem::Result& result) const {
    uint64_t h = 0xcbf29ce484222325ull;
    h ^= static_cast<uint64_t>(result.pairs.size()) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= static_cast<uint64_t>(result.outlineVertices.size()) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= static_cast<uint64_t>(result.correspondenceVertices.size()) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);

    auto hashVertices = [&](const std::vector<ContactInterface::ContactLineVertex>& vertices) {
        if (vertices.empty()) {
            h ^= 0x1234u + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return;
        }
        const size_t indices[3] = { 0, vertices.size() / 2, vertices.size() - 1 };
        for (size_t idx : indices) {
            const auto& p = vertices[idx].position;
            const auto& c = vertices[idx].color;
            h = combineFloat(h, p.x);
            h = combineFloat(h, p.y);
            h = combineFloat(h, p.z);
            h = combineFloat(h, c.x);
            h = combineFloat(h, c.y);
            h = combineFloat(h, c.z);
        }
    };

    hashVertices(result.outlineVertices);
    hashVertices(result.correspondenceVertices);
    return h;
}
