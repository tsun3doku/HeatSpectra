#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "HeatSystemPresets.hpp"

class VulkanDevice;
class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class SceneRenderer;
class SwapchainManager;
class CommandPool;
class FrameSync;
class FrameSimulation;
class HeatSystem;
class ModelUploader;
class MeshModifiers;
class VkFrameGraphRuntime;

class HeatSystemController {
public:
    HeatSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ResourceManager& resourceManager,
        MeshModifiers& meshModifiers,
        ModelUploader& modelUploader,
        UniformBufferManager& uniformBufferManager,
        SceneRenderer& sceneRenderer,
        SwapchainManager& swapchainManager,
        VkFrameGraphRuntime& frameGraphRuntime,
        CommandPool& renderCommandPool,
        FrameSync& frameSync,
        std::unique_ptr<HeatSystem>& heatSystem,
        std::atomic<bool>& isOperating,
        uint32_t maxFramesInFlight);

    bool isHeatSystemActive() const;
    bool isHeatSystemPaused() const;

    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t modelId);
    void setActiveModels(const std::vector<uint32_t>& sourceModelIds, const std::vector<uint32_t>& receiverModelIds);
    void setMaterialBindings(const std::vector<HeatModelMaterialBindings>& bindings);

    void createHeatSystem(VkExtent2D extent, VkRenderPass renderPass);
    void recreateHeatSystem(VkExtent2D extent, VkRenderPass renderPass);

    FrameSimulation* getHeatSystem() const;

private:
    std::unique_ptr<HeatSystem> buildHeatSystem(VkExtent2D extent, VkRenderPass renderPass);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;

    MeshModifiers& meshModifiers;
    ModelUploader& modelUploader;
    SceneRenderer& sceneRenderer;

    SwapchainManager& swapchainManager;
    VkFrameGraphRuntime& frameGraphRuntime;
    CommandPool& renderCommandPool;
    FrameSync& frameSync;

    std::unique_ptr<HeatSystem>& heatSystem;
    std::vector<uint32_t> configuredSourceModelIds;
    std::vector<uint32_t> configuredReceiverModelIds;
    std::vector<HeatModelMaterialBindings> configuredMaterialBindings;

    std::atomic<bool>& isOperating;
    const uint32_t maxFramesInFlight;
};

