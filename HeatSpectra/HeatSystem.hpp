#pragma once

#include "Structs.hpp"
#include "VoxelGrid.hpp"
#include <unordered_map>
#include "iODT.hpp"
#include "HashGrid.hpp"
#include "VoronoiSeeder.hpp"

constexpr float AMBIENT_TEMPERATURE = 1.0f;
static constexpr int NUM_SUBSTEPS = 10;


class Camera;
class HeatSource;
class HeatReceiver;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class SurfelRenderer;
class HashGridRenderer;
class VoronoiRenderer;
class PointRenderer;
class LloydCompute;
class VoronoiGeoCompute;
class VoronoiIntegrator;

class HeatSystem {
public:

    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
        UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& renderCommandPool,
        VkExtent2D extent, VkRenderPass renderPass);
    ~HeatSystem();
    void update(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT);
    void recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass);
    void processResetRequest();
    void requestReset();
    void setActive(bool active);

    void addReceiver(Model* model);
    void executeBufferTransfers();
    void createTimeBuffer();
    void initializeVoronoi();
    
    void generateVoronoiDiagram(bool enableLloyd);
    void exportDebugCellsToOBJ();
    void exportCellVolumes();
    void exportVoronoiDumpInfo(); 

    void createVoronoiDescriptorPool(uint32_t maxFramesInFlight);
    void createVoronoiDescriptorSetLayout();
    void createVoronoiDescriptorSets(uint32_t maxFramesInFlight);
    void createVoronoiPipeline();

    void buildVoronoiNeighborBuffer();

    void createSurfaceDescriptorPool(uint32_t maxFramesInFlight);
    void createSurfaceDescriptorSetLayout();
    void createSurfacePipeline();

    void createContactDescriptorPool(uint32_t maxFramesInFlight);
    void createContactDescriptorSetLayout();
    void createContactPipeline();
    
    void createHeatRenderDescriptorPool(uint32_t maxFramesInFlight);
    void createHeatRenderDescriptorSetLayout();
    void createHeatRenderDescriptorSets(uint32_t maxFramesInFlight);
    void createHeatRenderPipeline(VkExtent2D extent, VkRenderPass renderPass);

    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    
    void renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, int radius);
    void renderHashGrids(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent);
    
    // Getters for Voronoi
    VkBuffer getSeedPositionBuffer() const { return seedPositionBuffer; }
    VkDeviceSize getSeedPositionBufferOffset() const { return seedPositionBufferOffset_; }
    VkBuffer getVoronoiNodeBuffer() const { return voronoiNodeBuffer; }
    VkDeviceSize getVoronoiNodeBufferOffset() const  { return voronoiNodeBufferOffset_; }
    VkBuffer getVoronoiNeighborBuffer() const { return neighborIndicesBuffer; } 
    VkDeviceSize getVoronoiNeighborBufferOffset() const { return neighborIndicesBufferOffset_; }
    uint32_t getVoronoiNodeCount() const { return voronoiNodeCount; }

    void createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    // Getters
    VkPipeline getHeatRenderPipeline() const { return heatRenderPipeline; }
    VkPipelineLayout getHeatRenderPipelineLayout() const { return heatRenderPipelineLayout; }
    const std::vector<VkDescriptorSet>& getHeatRenderDescriptorSets() const { return heatRenderDescriptorSets; }
    const std::vector<std::unique_ptr<HeatReceiver>>& getReceivers() const { return receivers; }
    std::vector<std::unique_ptr<HeatReceiver>>& getReceivers() { return receivers; }
    HeatSource& getHeatSource() { return *heatSource; }

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const { return computeCommandBuffers; }

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return isPaused; } 
    void setIsPaused(bool paused) { isPaused = paused; } 
    bool getIsVoronoiReady() const { return isVoronoiReady; }  


private:    
    void initializeSurfelRenderers(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeHashGridRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void initializeLloydCompute();
    void initializeVoronoiGeoCompute();
    void createVoronoiGeometryBuffers(const VoronoiIntegrator& integrator, const std::vector<uint32_t>& seedFlags);
    void uploadOccupancyPoints(const VoxelGrid& voxelGrid);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool; 
    std::unique_ptr<HeatSource> heatSource;
    std::vector<std::unique_ptr<HeatReceiver>> receivers;
    Camera* camera = nullptr;
    
    std::unique_ptr<HashGridRenderer> hashGridRenderer;
    std::unique_ptr<VoronoiRenderer> voronoiRenderer;
    std::unique_ptr<PointRenderer> pointRenderer;
    std::unique_ptr<LloydCompute> lloydCompute;
    std::unique_ptr<VoronoiGeoCompute> voronoiGeoCompute;
    
    uint32_t maxFramesInFlight;
 
    std::unique_ptr<VoronoiSeeder> voronoiSeeder;
    std::unique_ptr<class VoronoiIntegrator> voronoiIntegrator; 
    bool isVoronoiSeederReady = false;
    bool isVoronoiReady = false; 
    
    uint32_t voronoiNodeCount = 0; 
    
    VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNodeBufferOffset_ = 0;
    void* mappedVoronoiNodeData = nullptr;
    
    VkBuffer voronoiNeighborBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNeighborBufferOffset_ = 0;
    
    VkDescriptorPool voronoiDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> voronoiDescriptorSets;
    std::vector<VkDescriptorSet> voronoiDescriptorSetsB;  
    
    VkPipelineLayout voronoiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline voronoiPipeline = VK_NULL_HANDLE;

    VkBuffer debugCellGeometryBuffer;
    VkDeviceSize debugCellGeometryBufferOffset_;
    void* mappedDebugCellGeometryData = nullptr;
    
    VkBuffer voronoiDumpBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiDumpBufferOffset_ = 0;
    void* mappedVoronoiDumpData = nullptr;

    VkBuffer voronoiDebugCellIdsBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiDebugCellIdsBufferOffset_ = 0;
    void* mappedVoronoiDebugCellIdsData = nullptr;

    VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize neighborIndicesBufferOffset_;
    static constexpr int K_NEIGHBORS = 50;  
    
    VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceAreasBufferOffset_;
    void* mappedInterfaceAreasData = nullptr;
    
    VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceNeighborIdsBufferOffset_;
    void* mappedInterfaceNeighborIdsData = nullptr;
    
    VkBuffer meshTriangleBuffer = VK_NULL_HANDLE;
    VkDeviceSize meshTriangleBufferOffset_;
    
    VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedPositionBufferOffset_;
    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset_;
    void* mappedSeedPositionData = nullptr;
    
    VkBuffer relevantMeshIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize relevantMeshIndicesBufferOffset_;
    
    VkBuffer spatialInfoBuffer = VK_NULL_HANDLE;
    VkDeviceSize spatialInfoBufferOffset_;

    VkBuffer voxelGridParamsBuffer = VK_NULL_HANDLE;           
    VkDeviceSize voxelGridParamsBufferOffset_;
    
    VkBuffer voxelOccupancyBuffer = VK_NULL_HANDLE;            
    VkDeviceSize voxelOccupancyBufferOffset_;
    
    VkBuffer voxelMeshPointsBuffer = VK_NULL_HANDLE;           
    VkDeviceSize voxelMeshPointsBufferOffset_;
    
    VkBuffer voxelMeshTrianglesBuffer = VK_NULL_HANDLE;         
    VkDeviceSize voxelMeshTrianglesBufferOffset_;
    
    VkBuffer voxelTrianglesListBuffer = VK_NULL_HANDLE;       
    VkDeviceSize voxelTrianglesListBufferOffset_;
    
    VkBuffer voxelOffsetsBuffer = VK_NULL_HANDLE;              
    VkDeviceSize voxelOffsetsBufferOffset_;
    
    std::vector<VkCommandBuffer> computeCommandBuffers;
    
    VkBuffer tempBufferA = VK_NULL_HANDLE;
    VkDeviceSize tempBufferAOffset_ = 0;
    void* mappedTempBufferA = nullptr;
    
    VkBuffer tempBufferB = VK_NULL_HANDLE;
    VkDeviceSize tempBufferBOffset_ = 0;
    void* mappedTempBufferB = nullptr;

    VkBuffer injectionKBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKBufferOffset_ = 0;
    void* mappedInjectionKBuffer = nullptr;

    VkBuffer injectionKTBuffer = VK_NULL_HANDLE;
    VkDeviceSize injectionKTBufferOffset_ = 0;
    void* mappedInjectionKTBuffer = nullptr;

    VkBuffer timeBuffer = VK_NULL_HANDLE;
    VkDeviceSize timeBufferOffset_ = 0;
    void* mappedTimeData = nullptr;

    VkDescriptorPool surfaceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout surfaceDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout surfacePipelineLayout = VK_NULL_HANDLE;
    VkPipeline surfacePipeline = VK_NULL_HANDLE;

    VkDescriptorPool contactDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout contactDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout contactPipelineLayout = VK_NULL_HANDLE;
    VkPipeline contactPipeline = VK_NULL_HANDLE;
    
    VkDescriptorPool heatRenderDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout heatRenderDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> heatRenderDescriptorSets;
    
    VkPipelineLayout heatRenderPipelineLayout = VK_NULL_HANDLE;
    VkPipeline heatRenderPipeline = VK_NULL_HANDLE;

    bool isActive = false;
    bool isPaused = false;
    std::atomic<bool> needsReset{ 
        false 
    };
};
