#pragma once

#include "ContactSystem.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class CommandPool;
class ContactLineRenderer;
class MemoryAllocator;
class ModelRegistry;
class Remesher;
class ResourceManager;
class UniformBufferManager;
class VulkanDevice;

class ContactSystemController {
public:
    ContactSystemController(
        ModelRegistry& modelRegistry,
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ResourceManager& resourceManager,
        Remesher& remesher,
        UniformBufferManager& uniformBufferManager,
        CommandPool& renderCommandPool);
    ~ContactSystemController();

    void beginPreviewFrame();
    void endPreviewFrame();

    bool updatePreviewForNodeModels(
        uint32_t ownerNodeId,
        ContactCouplingKind kind,
        uint32_t emitterNodeModelId,
        uint32_t receiverNodeModelId,
        float minNormalDot,
        float contactRadius,
        std::vector<ContactPairGPU>& outPairs,
        bool forceRebuild = false);

    bool computePairsForRuntimeModels(
        const ConfiguredContactPair& pair,
        std::vector<ContactPairGPU>& outPairs,
        bool forceRebuild = false);

    void initRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void reinitRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void clearRenderer();
    void renderLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) const;

    void clearCache();

private:
    void rebuildPreviewBuffers();

    ModelRegistry& modelRegistry;
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    UniformBufferManager& uniformBufferManager;
    ContactSystem contactSystem;
    std::unordered_map<uint32_t, ContactSystem::Result> previewResultsByNodeId;
    std::unique_ptr<ContactLineRenderer> contactLineRenderer;
};
