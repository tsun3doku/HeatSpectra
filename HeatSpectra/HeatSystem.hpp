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
    void update(VulkanDevice& vulkanDevice, bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT);
    void recreateResources(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void processResetRequest();
    void requestReset();
    void setActive(bool active);

    void generateTetrahedralMesh(ResourceManager& resourceManager);
    void initializeSurfaceBuffer(ResourceManager& resourceManager);
    void initializeTetra(VulkanDevice& vulkanDevice);

    void createTetraBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createNeighborBuffer(VulkanDevice& vulkanDevice);
    void createCenterBuffer(VulkanDevice& vulkanDevice);
    void createTimeBuffer(VulkanDevice& vulkanDevice);

    void createTetraDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createTetraDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createTetraDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createTetraPipeline(const VulkanDevice& vulkanDevice);

    void createSurfaceDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createSurfaceDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createSurfaceDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight);
    void createSurfacePipeline(const VulkanDevice& vulkanDevice);

    void dispatchTetraCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void dispatchSurfaceCompute(VkCommandBuffer commandBuffer, ResourceManager& resourceManager, uint32_t currentFrame);
    void recordComputeCommands(VkCommandBuffer commandBuffer, ResourceManager& resourceManager, uint32_t currentFrame);

    glm::vec3 calculateTetraCenter(const TetrahedralElement& tetra);
    float calculateTetraVolume(const TetrahedralElement& tetra);

    void createComputeCommandBuffers(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    void cleanupResources(VulkanDevice& vulkanDevice);
    void cleanup(VulkanDevice& vulkanDevice);

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

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    std::unique_ptr<HeatSource> heatSource;
    Camera* camera = nullptr;

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
    std::atomic<bool> needsReset{ 
        false 
    };
};