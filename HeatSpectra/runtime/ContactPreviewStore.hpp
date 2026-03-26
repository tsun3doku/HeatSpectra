#pragma once

#include "contact/ContactSystem.hpp"
#include "renderers/ContactLineRenderer.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;

class ContactPreviewStore {
public:
    ContactPreviewStore(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager);
    ~ContactPreviewStore();

    void endFrame();

    void setPreviewForNode(uint32_t ownerNodeId, const ContactSystem::Result& result);
    void clearPreviewForNode(uint32_t ownerNodeId);
    void clearAllPreviews();

    void initRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void reinitRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void renderLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) const;

private:
    static uint64_t combineFloat(uint64_t hash, float value);
    uint64_t computePreviewHash(const ContactSystem::Result& result) const;
    void rebuildPreviewBuffers();
    void clearRenderer();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    std::unique_ptr<ContactLineRenderer> contactLineRenderer;
    std::unordered_map<uint32_t, ContactSystem::Result> previewResultsByNodeId;
    std::unordered_map<uint32_t, uint64_t> previewHashByNodeId;
    uint64_t previewRevision = 0;
    uint64_t lastBuiltRevision = 0;
};
