#pragma once

#include "util/Structs.hpp"
#include "HeatSystemRuntime.hpp"
#include "HeatSystemPresets.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include "framegraph/FrameSimulation.hpp"

#include <unordered_map>

constexpr float AMBIENT_TEMPERATURE = 1.0f;
static constexpr int NUM_SUBSTEPS = 8;

class Model;
class HeatSource;
class HeatReceiver;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class SurfelRenderer;
class VoronoiRenderer;
class PointRenderer;
class ContactLineRenderer;
class HeatRenderer;
class ContactInterface;
class VoronoiGeoCompute;
class VoronoiCandidateCompute;
class Remesher;
class HeatSystemContactStage;
class HeatSystemSurfaceStage;
class HeatSystemDebugStage;
class HeatSystemRenderStage;

class HeatSystem : public FrameSimulation {
public:

    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, Remesher& remesher,
        UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& renderCommandPool,
        VkExtent2D extent, VkRenderPass renderPass);
    ~HeatSystem();

    void update() override;
    void recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) override;
    void processResetRequest() override;
    void requestReset();
    void setActive(bool active);
    bool isInitialized() const { return initialized; }

    void executeBufferTransfers();
    bool createTimeBuffer();
    void initializeVoronoi();
    
    bool generateVoronoiDiagram();
    void exportDebugCellsToOBJ();
    void exportCellVolumes();
    void exportVoronoiDumpInfo(); 

    bool createVoronoiDescriptorPool(uint32_t maxFramesInFlight);
    bool createVoronoiDescriptorSetLayout();
    bool createVoronoiDescriptorSets(uint32_t maxFramesInFlight);
    bool createVoronoiPipeline();

    bool createSurfaceDescriptorPool(uint32_t maxFramesInFlight);
    bool createSurfaceDescriptorSetLayout();
    bool createSurfacePipeline();

    bool createContactDescriptorPool(uint32_t maxFramesInFlight);
    bool createContactDescriptorSetLayout();
    bool createContactPipeline();

    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool = VK_NULL_HANDLE, uint32_t timingQueryBase = 0) override;
    
    void renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, float radius) override;
    void renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex) override;
    void renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex) override;
    void renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) override;
    void renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) override;
    
    VkBuffer getSeedPositionBuffer() const { return resources.seedPositionBuffer; }
    VkDeviceSize getSeedPositionBufferOffset() const { return resources.seedPositionBufferOffset_; }
    VkBuffer getVoronoiNodeBuffer() const { return resources.voronoiNodeBuffer; }
    VkDeviceSize getVoronoiNodeBufferOffset() const  { return resources.voronoiNodeBufferOffset_; }
    VkBuffer getVoronoiNeighborBuffer() const { return resources.neighborIndicesBuffer; } 
    VkDeviceSize getVoronoiNeighborBufferOffset() const { return resources.neighborIndicesBufferOffset_; }
    uint32_t getVoronoiNodeCount() const { return resources.voronoiNodeCount; }

    bool createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    const std::vector<std::unique_ptr<HeatReceiver>>& getReceivers() const { return receivers; }
    std::vector<std::unique_ptr<HeatReceiver>>& getReceivers() { return receivers; }

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const override { return resources.computeCommandBuffers; }

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return isPaused; } 
    void setIsPaused(bool paused) { isPaused = paused; } 
    bool getIsVoronoiReady() const { return isVoronoiReady; }  
    void setDebugEnabled(bool enabled) { debugEnable = enabled; }
    bool getDebugEnabled() const { return debugEnable; }
    bool hasDispatchableComputeWork() const override { return isActive && !isPaused && isVoronoiReady; }
    bool simulationActive() const override { return isActive; }
    bool simulationPaused() const override { return isPaused; }
    bool voronoiReady() const override { return isVoronoiReady; }
    void setActiveModels(const std::vector<uint32_t>& sourceModelIds, const std::vector<uint32_t>& receiverModelIds);
    void setMaterialBindings(const std::vector<HeatModelMaterialBindings>& bindings);

private:    
    using SourceCoupling = HeatSystemRuntime::SourceCoupling;
    using ContactCoupling = HeatSystemRuntime::ContactCoupling;

    void failInitialization(const char* stage);
    const HeatSystemVoronoiDomain* findReceiverDomain(const HeatReceiver* receiver) const;
    const HeatSystemVoronoiDomain* findReceiverDomainByModelId(uint32_t receiverModelId) const;
    void clearReceiverDomains();
    void updateCouplingDescriptors(ContactCoupling& coupling, uint32_t nodeCount);
    void initializeHeatModelBindings();

    void initializeContactInterface();

    void initializeSurfelRenderers(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeContactLineRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeVoronoiGeoCompute();
    void initializeVoronoiCandidateCompute();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    Remesher& remesher;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool; 
    HeatSystemRuntime runtime;
    std::vector<SourceCoupling>& heatSources;
    std::vector<std::unique_ptr<HeatReceiver>>& receivers;
    std::vector<ContactCoupling>& contactCouplings;
    std::vector<uint32_t>& receiverModelIds;
    std::vector<uint32_t> activeSourceModelIds;
    std::vector<uint32_t> activeReceiverModelIds;
    std::vector<HeatSystemVoronoiDomain> receiverVoronoiDomains;
    std::unordered_map<uint32_t, HeatMaterialPresetId> receiverMaterialPresetByModelId;
    
    std::unique_ptr<VoronoiRenderer> voronoiRenderer;
    std::unique_ptr<PointRenderer> pointRenderer;
    std::unique_ptr<ContactLineRenderer> contactLineRenderer;
    std::unique_ptr<HeatRenderer> heatRenderer;
	std::unique_ptr<ContactInterface> contactInterface;
    std::unique_ptr<VoronoiGeoCompute> voronoiGeoCompute;
    std::unique_ptr<class VoronoiCandidateCompute> voronoiCandidateCompute;
    std::unique_ptr<HeatSystemVoronoiStage> voronoiStage;
    std::unique_ptr<HeatSystemContactStage> contactStage;
    std::unique_ptr<HeatSystemSurfaceStage> surfaceStage;
    std::unique_ptr<HeatSystemDebugStage> debugStage;
    std::unique_ptr<HeatSystemRenderStage> renderStage;
    HeatSystemResources resources;
    
    uint32_t maxFramesInFlight;
 
    bool isVoronoiSeederReady = false;
    bool isVoronoiReady = false; 
    static constexpr int K_NEIGHBORS = 50;  

    bool isActive = false;
    bool isPaused = false;
    bool initialized = false;
    bool debugEnable = false;
    std::atomic<bool> needsReset{ 
        false 
    };
};
