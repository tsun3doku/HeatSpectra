#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "contact/ContactTypes.hpp"
#include "domain/HeatData.hpp"
#include "HeatSystemPresets.hpp"
#include "HeatSystemResources.hpp"
#include "domain/RemeshData.hpp"
#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimePackages.hpp"

class VulkanDevice;
class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class CommandPool;
class HeatSystem;
class ContactPreviewStore;
class RenderRuntime;
class RuntimeIntrinsicCache;

class HeatSystemController {
public:
    HeatSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ResourceManager& resourceManager,
        UniformBufferManager& uniformBufferManager,
        RuntimeIntrinsicCache& remeshResources,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    bool isHeatSystemActive() const;
    bool isHeatSystemPaused() const;

    void applyHeatPackage(const HeatPackage& heatPackage);
    void applyResolvedContacts(const std::vector<RuntimeContactResult>& resolvedContacts);
    void setContactPreviewStore(ContactPreviewStore* contactPreviewStore);
    HeatSystem* getHeatSystem() const;

    void createHeatSystem(VkExtent2D extent, VkRenderPass renderPass);
    void recreateHeatSystem(VkExtent2D extent, VkRenderPass renderPass);
    void destroyHeatSystem();

private:
    std::unique_ptr<HeatSystem> buildHeatSystem(VkExtent2D extent, VkRenderPass renderPass);
    void configureHeatSystem(HeatSystem& system);
    void applyHeatActivationState(HeatSystem& system);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    RuntimeIntrinsicCache& remeshResources;
    CommandPool& renderCommandPool;

    std::unique_ptr<HeatSystem> heatSystem;
    HeatSystemResources heatSystemResources;
    HeatPackage heatPackageStorage{};
    std::vector<RuntimeContactResult> resolvedContactsStorage;
    bool hasConfiguredHeatPackage = false;
    ContactPreviewStore* contactPreviewStore = nullptr;
    const uint32_t maxFramesInFlight;
};

