#pragma once
#include "TetGen/tetgen.h"
#include "Structs.hpp"

class Camera;
class HeatSource;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;

class HeatSystem {
public:
    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight);
    ~HeatSystem();
    void update(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT);
    void recreateResources(uint32_t maxFramesInFlight);
    void processResetRequest();
    void requestReset();
    void setActive(bool active);

    void generateTetrahedralMesh();
    void initializeSurfaceBuffer();
    void initializeTetra();

    void createTetraBuffer(uint32_t maxFramesInFlight);
    void createNeighborBuffer();
    void createCenterBuffer();
    void createTimeBuffer();

    void createTetraDescriptorPool(uint32_t maxFramesInFlight);
    void createTetraDescriptorSetLayout();
    void createTetraDescriptorSets(uint32_t maxFramesInFlight);
    void createTetraPipeline();

    void createSurfaceDescriptorPool(uint32_t maxFramesInFlight);
    void createSurfaceDescriptorSetLayout();
    void createSurfaceDescriptorSets(uint32_t maxFramesInFlight);
    void createSurfacePipeline();

    void dispatchTetraCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void dispatchSurfaceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    glm::vec3 calculateTetraCenter(const TetrahedralElement& tetra);
    float calculateTetraVolume(const TetrahedralElement& tetra);

    void createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    // Getters
    VkPipeline getHeatPipeline() const {
        return tetraPipeline;
    }
    VkPipelineLayout getHeatPipelineLayout() const {
        return tetraPipelineLayout;
    }
    const std::vector<VkDescriptorSet>& getHeatDescriptorSets() const {
        return tetraDescriptorSets;
    }
    HeatSource& getHeatSource() {
        return *heatSource;
    }

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const {
        return computeCommandBuffers;
    }

    bool getIsActive() const { 
        return isActive; 
    }
    
    bool getIsPaused() const {
        return isPaused;
    }
    
    void setIsPaused(bool paused) {
        isPaused = paused;
    }
    
    bool getIsTetMeshReady() const {
        return isTetMeshReady;
    }   

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    std::unique_ptr<HeatSource> heatSource;
    Camera* camera = nullptr;
    
    uint32_t maxFramesInFlight;

    FEAMesh feaMesh;
    std::vector<int> remappedIndices;
    std::unordered_map<glm::vec3, int> vertexMap;
    std::vector<glm::vec3> tetraCenters;

    std::vector<VkCommandBuffer> computeCommandBuffers;

    TetraFrameBuffers tetraFrameBuffers;
    VkBuffer tetraBuffer;
    VkDeviceMemory tetraBufferMemory;
    VkDeviceSize tetraBufferOffset_;
    TetrahedralElement* mappedTetraData = nullptr;

    VkBuffer neighborBuffer;
    VkDeviceMemory neighborBufferMemory;
    VkDeviceSize neighborBufferOffset_;
    const uint32_t MAX_NEIGHBORS = 4;

    VkBuffer centerBuffer;
    VkDeviceSize centerBufferOffset_;
    VkDeviceMemory centerBufferMemory;

    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    VkDeviceSize timeBufferOffset_;
    TimeUniform* mappedTimeData = nullptr;

    VkDescriptorPool tetraDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout tetraDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> tetraDescriptorSets;

    VkPipelineLayout tetraPipelineLayout = VK_NULL_HANDLE;
    VkPipeline tetraPipeline = VK_NULL_HANDLE;

    VkDescriptorPool surfaceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout surfaceDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> surfaceDescriptorSets;

    VkPipelineLayout surfacePipelineLayout = VK_NULL_HANDLE;
    VkPipeline surfacePipeline = VK_NULL_HANDLE;

    bool isActive = false;
    bool isPaused = false;
    std::atomic<bool> needsReset{ 
        false 
    };
    bool isTetMeshReady = false;
};