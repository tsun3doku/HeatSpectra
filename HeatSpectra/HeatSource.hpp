#pragma once

class VulkanDevice;
class MemoryAllocator;
class Model;

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSource {
public:
    HeatSource(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, uint32_t maxFramesInFlight);
    ~HeatSource();

    void recreateResources(uint32_t maxFramesInFlight);

    void createSourceBuffer();
    void initializeSurfaceBuffer();

    void controller(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, float deltaTime);

    void createHeatSourceDescriptorPool(uint32_t maxFramesInFlight);
    void createHeatSourceDescriptorSets(uint32_t maxFramesInFlight);
    void createHeatSourcePipeline();
    void createHeatSourceDescriptorSetLayout();

    void dispatchSourceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void cleanupResources();
    void cleanup();

    // Getters
    size_t getVertexCount() const {
        return heatModel.getVertexCount();
    }
    VkBuffer getVertexBuffer() const { 
        return heatModel.getVertexBuffer(); 
    }
    VkBuffer getIndexBuffer() const { 
        return heatModel.getIndexBuffer(); 
    }
    size_t getIndexCount() const {
        return heatModel.getIndices().size();
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
    Model& heatModel;

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