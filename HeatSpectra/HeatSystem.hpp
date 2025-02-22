#pragma once
#include "TetGen/tetgen.h"

class Camera;
class HeatSource;
class ResourceManager;
class MemoryAllocator;
class VulkanDevice;

class HeatSystem {
public:
    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, uint32_t maxFramesInFlight);
    ~HeatSystem();
    void update(VulkanDevice& vulkanDevice, GLFWwindow* window, ResourceManager& resourceManager, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT);
    void recreateResources(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void swapBuffers(ResourceManager& resourceManager);

    void generateTetrahedralMesh(ResourceManager& resourceManager);
    void initializeSurfaceBuffer(ResourceManager& resourceManager);
    void initializeTetra(VulkanDevice& vulkanDevice);

    void createTetraBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createNeighborBuffer(VulkanDevice& vulkanDevice);
    void createMeshBuffer(VulkanDevice& vulkanDevice);
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
    std::vector<VkDescriptorSet>& getHeatDescriptorSets() {
        return tetraDescriptorSets;
    }
    HeatSource& getHeatSource() { 
        return *heatSource; 
    }

    std::vector<VkCommandBuffer> getComputeCommandBuffers() {
        return computeCommandBuffers;
    }

private:
    VulkanDevice* vulkanDevice = nullptr;
    MemoryAllocator* memoryAllocator = nullptr;
    ResourceManager& resourceManager;
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

    VkBuffer meshBuffer;
    VkDeviceMemory meshBufferMemory;

    VkBuffer centerBuffer;
    VkDeviceSize centerBufferOffset_;
    VkDeviceMemory centerBufferMemory;

    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    VkDeviceSize timeBufferOffset_;
    TimeUniform* mappedTimeData = nullptr;

    VkDescriptorPool tetraDescriptorPool;
    VkDescriptorSetLayout tetraDescriptorSetLayout;
    std::vector<VkDescriptorSet> tetraDescriptorSets;

    VkPipelineLayout tetraPipelineLayout;
    VkPipeline tetraPipeline;

    VkDescriptorPool surfaceDescriptorPool;
    VkDescriptorSetLayout surfaceDescriptorSetLayout;
    std::vector<VkDescriptorSet> surfaceDescriptorSets;

    VkPipelineLayout surfacePipelineLayout;
    VkPipeline surfacePipeline;
};