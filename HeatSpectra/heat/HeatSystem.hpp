#pragma once

#include "HeatContactRuntime.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemSurfaceRuntime.hpp"
#include "HeatSystemRuntime.hpp"
#include "HeatSystemPresets.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeThermalTypes.hpp"
#include "voronoi/VoronoiDomain.hpp"

#include <atomic>
#include <memory>
#include <unordered_map>

static constexpr int NUM_SUBSTEPS = 8;

class HeatSourceRuntime;
class HeatReceiverRuntime;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class HeatReceiverRenderer;
class HeatSourceRenderer;
class PointRenderer;
class ContactPreviewStore;
class HeatSystemContactStage;
class HeatSystemSimStage;
class HeatSystemSurfaceStage;
class HeatSystemRenderStage;
class HeatSystemVoronoiStage;
class RuntimeIntrinsicCache;
class HeatSystemResources;
class VoronoiBuilder;
class VoronoiGeoCompute;
class VoronoiModelRuntime;

class HeatSystem {
public:

    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, RuntimeIntrinsicCache& remeshResources, HeatSystemResources& resources,
        UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& renderCommandPool,
        VkExtent2D extent, VkRenderPass renderPass);
    ~HeatSystem();

    void update();
    void recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass);
    void processResetRequest();
    void requestHeatReset();
    void setActive(bool active);
    bool isInitialized() const { return initialized; }

    bool createContactDescriptorPool(uint32_t maxFramesInFlight);
    bool createContactDescriptorSetLayout();
    bool createContactPipeline();

    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool = VK_NULL_HANDLE, uint32_t timingQueryBase = 0);
    
    void renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, float radius);
    void renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent);
    
    bool createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    const std::vector<HeatSystemRuntime::SourceBinding>& getSourceBindings() const { return heatSources; }

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const { return computeCommandBuffers; }

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return isPaused; } 
    void setIsPaused(bool paused) { isPaused = paused; } 
    void setDebugEnabled(bool enabled) { debugEnable = enabled; }
    bool getDebugEnabled() const { return debugEnable; }
    bool hasDispatchableComputeWork() const;
    bool voronoiReady() const;
    void setContactPreviewStore(ContactPreviewStore* store);
    void setHeatPackage(const HeatPackage* package);
    void configureSourceRuntimes(const HeatPackage& heatPackage);
    void setResolvedContacts(const std::vector<RuntimeContactResult>& contacts);
    void setThermalMaterials(const std::vector<RuntimeThermalMaterial>& materials);
    void setSourceTemperatures(const std::unordered_map<uint32_t, float>& temperatures);

private:    
    using SourceBinding = HeatSystemRuntime::SourceBinding;
    using ContactCoupling = HeatContactRuntime::ContactCoupling;
    using ContactCouplingType = ::ContactCouplingType;

    void failInitialization(const char* stage);
    void processHeatReset();
    void markHeatStructureDirty();
    void rebuildHeatContactCouplings();
    bool rebuildHeatStateRuntimes(bool forceDescriptorReallocate);
    bool rebuildVoronoiRuntime();
    bool initializeVoronoiReceiverRuntimes();
    bool initializeVoronoiMaterialNodes();
    void uploadVoronoiModelStagingBuffers();
    void rebuildReceiverThermalMaterialMap();
    void cleanupVoronoiRuntime();
    void resetHeatState();
    static bool isSameContactPairData(const ContactPairData& lhs, const ContactPairData& rhs);
    static bool isSameContactPair(const ContactPair& lhs, const ContactPair& rhs);
    static bool isSameRuntimeContactBinding(const RuntimeContactBinding& lhs, const RuntimeContactBinding& rhs);
    static bool isSameRuntimeContactResult(const RuntimeContactResult& lhs, const RuntimeContactResult& rhs);
    static bool areRuntimeContactResultsEqual(const std::vector<RuntimeContactResult>& lhs, const std::vector<RuntimeContactResult>& rhs);
    static bool isSameRuntimeThermalMaterial(const RuntimeThermalMaterial& lhs, const RuntimeThermalMaterial& rhs);
    static bool areRuntimeThermalMaterialsEqual(
        const std::vector<RuntimeThermalMaterial>& lhs,
        const std::vector<RuntimeThermalMaterial>& rhs);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    RuntimeIntrinsicCache& remeshResources;
    HeatSystemResources& resources;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool; 
    HeatSystemRuntime runtime;
    HeatSystemSimRuntime simRuntime;
    HeatSystemSurfaceRuntime surfaceRuntime;
    std::vector<SourceBinding>& heatSources;
    HeatContactRuntime heatContactRuntime;
    std::vector<RuntimeContactResult> resolvedContacts;
    std::vector<RuntimeThermalMaterial> configuredThermalMaterials;
    std::unordered_map<uint32_t, float> sourceTemperatureByModelId;
    
    std::unique_ptr<HeatSourceRenderer> heatSourceRenderer;
    std::unique_ptr<HeatReceiverRenderer> heatReceiverRenderer;
    std::unique_ptr<PointRenderer> pointRenderer;
    std::unique_ptr<HeatSystemContactStage> contactStage;
    std::unique_ptr<HeatSystemSimStage> simStage;
    std::unique_ptr<HeatSystemSurfaceStage> surfaceStage;
    std::unique_ptr<HeatSystemVoronoiStage> voronoiStage;
    std::unique_ptr<HeatSystemRenderStage> renderStage;
    std::unique_ptr<VoronoiGeoCompute> voronoiGeoCompute;
    ContactPreviewStore* contactPreviewStore = nullptr;
    const HeatPackage* heatPackage = nullptr;
    std::unique_ptr<VoronoiBuilder> voronoiBuilder;
    std::vector<std::unique_ptr<VoronoiModelRuntime>> voronoiModelRuntimes;
    std::vector<VoronoiDomain> receiverVoronoiDomains;
    std::unordered_map<uint32_t, RuntimeThermalMaterial> receiverThermalMaterialByModelId;
    
    uint32_t maxFramesInFlight;
    std::vector<VkCommandBuffer> computeCommandBuffers;

    bool isActive = false;
    bool isPaused = false;
    bool initialized = false;
    bool debugEnable = false;
    bool heatStructureDirty = false;
    std::atomic<bool> needsReset{ false };
    static constexpr uint32_t MAX_NODE_NEIGHBORS = 50;
};
