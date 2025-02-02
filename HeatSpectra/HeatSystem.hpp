#pragma once
#include "TetGen/tetgen.h"
#include <unordered_map>

class VulkanDevice;
class UniformBufferManager;
class Model;
class Camera;

class HeatSystem {
public:
    void init(VulkanDevice& vulkanDevice, const UniformBufferManager& uniformBufferManager, Model& model, Camera& camera, uint32_t maxFramesInFLight);
    void update(VulkanDevice& vulkanDevice, Model& model, GLFWwindow* window, UniformBufferObject& ubo, uint32_t WIDTH, uint32_t HEIGHT);
    
    void createTetraDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createTetraDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createTetraDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createTetraPipeline(const VulkanDevice& vulkanDevice);

    void createSurfaceDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createSurfaceDescriptorSetLayout(const VulkanDevice& vulkanDevice);
    void createSurfaceDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createSurfacePipeline(const VulkanDevice& vulkanDevice);

    void dispatchTetraCompute(const VulkanDevice& vulkanDevice, Model& model, VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void dispatchSurfaceCompute(const VulkanDevice& vulkanDevice, Model& model, VkCommandBuffer commandBuffer, uint32_t currentFrame);
    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void generateTetrahedralMesh(Model& model);
    void createSurfaceBuffer(VulkanDevice& vulkanDevice);
    void createTetraBuffer(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void createProcessedBuffer(VulkanDevice& vulkanDevice);
    void createMeshBuffer(VulkanDevice& vulkanDevice);
    void createCenterBuffer(VulkanDevice& vulkanDevice);
    void createTimeBuffer(VulkanDevice& vulkanDevice);

    void swapBuffers();
    void initializeTetra(VulkanDevice& vulkanDevice);

    glm::vec3 calculateTetraCenter(const TetrahedralElement& tetra);

    void createComputeCommandBuffers(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    void cleanup(const VulkanDevice& vulkanDevice);

    VkPipeline getHeatPipeline() const { 
        return tetraPipeline; 
    }
    VkPipelineLayout getHeatPipelineLayout() const { 
        return tetraPipelineLayout; 
    }
    std::vector<VkDescriptorSet>& getHeatDescriptorSets() {
        return tetraDescriptorSets;
    }

    VkBuffer getSurfaceBuffer() const {
        return surfaceBuffer;
    }

    VkBuffer getSurfaceVertexBuffer() const {
        return surfaceVertexBuffer;
    }

    std::vector<VkCommandBuffer> getComputeCommandBuffers() {
        return computeCommandBuffers;
    }

private:
    VulkanDevice* vulkanDevice;
    const UniformBufferManager* uniformBufferManager;
    Model* model;
    Camera* camera;

    FEAMesh feaMesh;
    std::vector<int> remappedIndices;
    std::unordered_map<glm::vec3, int> vertexMap;
    std::vector<glm::vec3> tetraCenters;

    TetrahedralElement* mappedTetraData = nullptr;
    glm::vec3* mappedVertexColors = nullptr;

    std::vector<VkCommandBuffer> computeCommandBuffers;

    SurfaceVertex* mappedSurfaceVertices = nullptr;
    VkBuffer surfaceBuffer;
    VkDeviceMemory surfaceBufferMemory;

    VkBuffer tetraBuffer;
    VkDeviceMemory tetraBufferMemory;

    VkBuffer surfaceVertexBuffer;
    VkDeviceMemory surfaceVertexBufferMemory;

    VkBuffer processedBuffer;
    VkDeviceMemory processedBufferMemory;
    uint32_t* mappedProcessedData;

    VkBuffer meshBuffer;
    VkDeviceMemory meshBufferMemory;

    VkBuffer centerBuffer;
    VkDeviceMemory centerBufferMemory;

    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    TimeUniform* mappedTimeData;

    VkBuffer tetraWriteBuffer;
    VkDeviceMemory tetraWriteBufferMemory;
    uint32_t* mappedTetraWriteData;

    VkBuffer tetraReadBuffer;
    VkDeviceMemory tetraReadBufferMemory;
    uint32_t* mappedTetraReadData;

    TetraFrameBuffers tetraFrameBuffers;

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