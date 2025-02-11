#pragma once

class Model;
class VulkanDevice;

const std::string HEATSOURCE_PATH = "models/torus.obj";

class HeatSource {
public:
    void init(VulkanDevice& device, Model& heatModel, uint32_t maxFramesInFlight);

    void createSourceBuffer(VulkanDevice& vulkanDevice, Model& heatModel);

    void createHeatSourceDescriptorPool(VulkanDevice& device, uint32_t maxFramesInFlight);
    void createHeatSourceDescriptorSets(VulkanDevice& device, uint32_t maxFramesInFlight);
    void createHeatSourcePipeline(VulkanDevice& device);
    void createHeatSourceDescriptorSetLayout(VulkanDevice& device);

    void dispatchSourceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    void cleanup(VulkanDevice& vulkanDevice);

    // Getters
    size_t getVertexCount() {
        return heatModel->getVertexCount();
    }
    VkBuffer getVertexBuffer() { 
        return heatModel->getVertexBuffer(); 
    }
    VkBuffer getIndexBuffer() { 
        return heatModel->getIndexBuffer(); 
    }
    size_t getIndexCount() const {
        return heatModel->getIndices().size();
    }

    VkBuffer getSourceBuffer() const {
        return sourceBuffer;
    }
   
private:
    VulkanDevice* vulkanDevice;
    Model* heatModel;

    VkDescriptorPool heatSourceDescriptorPool;
    std::vector<VkDescriptorSet> heatSourceDescriptorSets;
    VkDescriptorSetLayout heatSourceDescriptorLayout;

    VkPipelineLayout heatSourcePipelineLayout;
    VkPipeline heatSourcePipeline;

    VkBuffer sourceBuffer;
    VkDeviceMemory sourceBufferMemory;

    VkBuffer heatSourceStagingBuffer;
    VkDeviceMemory heatSourceStagingMemory;

};