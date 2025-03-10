#pragma once

class VulkanDevice;
class MemoryAllocator;
class Model;

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSource {
public:
    HeatSource(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, uint32_t maxFramesInFlight);
    ~HeatSource();

    void recreateResources(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);

    void createSourceBuffer(VulkanDevice& vulkanDevice, Model& heatModel);
    void initializeSurfaceBuffer(Model& heatModel);

    void controller(GLFWwindow* window, float deltaTime);

    void createHeatSourceDescriptorPool(VulkanDevice& device, uint32_t maxFramesInFlight);
    void createHeatSourceDescriptorSets(VulkanDevice& device, uint32_t maxFramesInFlight);
    void createHeatSourcePipeline(VulkanDevice& device);
    void createHeatSourceDescriptorSetLayout(VulkanDevice& device);

    void dispatchSourceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void cleanupResources(VulkanDevice& vulkanDevice);
    void cleanup(VulkanDevice& vulkanDevice);

    // Getters
    size_t getVertexCount() const {
        return heatModel->getVertexCount();
    }
    VkBuffer getVertexBuffer() const { 
        return heatModel->getVertexBuffer(); 
    }
    VkBuffer getIndexBuffer() const { 
        return heatModel->getIndexBuffer(); 
    }
    size_t getIndexCount() const {
        return heatModel->getIndices().size();
    }

    VkBuffer getSourceBuffer() const {
        return sourceBuffer;
    }
    VkDeviceSize getSourceBufferOffset() const {
        return sourceBufferOffset_;
    }
    const HeatSourcePushConstant getHeatSourcePushConstant() const {
        return heatSourcePushConstant;
    }
    
    // Setters
    void setHeatSourcePushConstant(glm::mat4 modelMatrix) {
        heatSourcePushConstant.model = modelMatrix;
    }
   
private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model* heatModel = nullptr;

    HeatSourcePushConstant heatSourcePushConstant;

    VkDescriptorPool heatSourceDescriptorPool;
    std::vector<VkDescriptorSet> heatSourceDescriptorSets;
    VkDescriptorSetLayout heatSourceDescriptorLayout;

    VkPipelineLayout heatSourcePipelineLayout;
    VkPipeline heatSourcePipeline;

    VkBuffer sourceBuffer;
    VkDeviceMemory sourceBufferMemory;
    VkDeviceSize sourceBufferOffset_;

    VkBuffer heatSourceStagingBuffer;
    VkDeviceMemory heatSourceStagingMemory;

};