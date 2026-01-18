#pragma once

#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;
class ResourceManager;
class SupportingHalfedge;
class UniformBufferManager;

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSource {
public:
    HeatSource(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, ResourceManager& resourceManager, uint32_t maxFramesInFlight, CommandPool& renderCommandPool);
    ~HeatSource();

    void recreateResources(uint32_t maxFramesInFlight);

    void createSourceBuffer();
    void updateSourceBuffer(SupportingHalfedge* supportingHalfedge);

    void updateHeatRenderDescriptors(
        VkDescriptorSetLayout heatRenderDescriptorSetLayout,
        VkDescriptorPool heatRenderDescriptorPool,
        UniformBufferManager& uniformBufferManager,
        uint32_t maxFramesInFlight);

    void controller(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, float deltaTime);

    void cleanupResources();
    void cleanup();

    // Getters
    size_t getVertexCount() const;
    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
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
    VkBuffer getTriangleGeometryBuffer() const {
        return triangleGeometryBuffer;
    }
    VkDeviceSize getTriangleGeometryBufferOffset() const {
        return triangleGeometryBufferOffset_;
    }

    VkBuffer getTriangleCentroidBuffer() const {
        return triangleCentroidBuffer;
    }
    VkDeviceSize getTriangleCentroidBufferOffset() const {
        return triangleCentroidBufferOffset_;
    }
    uint32_t getTriangleCount() const {
        return triangleCount_;
    }
    const HeatSourcePushConstant getHeatSourcePushConstant() const {
        return heatSourcePushConstant;
    }
    
    const std::vector<VkDescriptorSet>& getHeatRenderDescriptorSets() const {
        return heatRenderDescriptorSets;
    }
    
    // Setters
    void setHeatSourcePushConstant(glm::mat4 modelMatrix) {
        heatSourcePushConstant.heatSourceModelMatrix = modelMatrix;
    }
   
private:
    static std::vector<float> calculateVertexAreas(
        size_t vertexCount,
        const std::vector<uint32_t>& indices,
        const std::function<glm::vec3(uint32_t)>& getVertexPosition);
    
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& heatModel;
    ResourceManager& resourceManager;
    CommandPool& renderCommandPool;  
    uint32_t maxFramesInFlight;
    size_t intrinsicVertexCount = 0;      

    HeatSourcePushConstant heatSourcePushConstant;

    std::vector<VkDescriptorSet> heatRenderDescriptorSets;  

    VkBuffer sourceBuffer;
    VkDeviceMemory sourceBufferMemory;
    VkDeviceSize sourceBufferOffset_;
    VkBufferView sourceBufferView = VK_NULL_HANDLE; 
    
    VkBuffer triangleGeometryBuffer;
    VkDeviceSize triangleGeometryBufferOffset_;

    VkBuffer triangleCentroidBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleCentroidBufferOffset_ = 0;
    uint32_t triangleCount_ = 0;

    VkBuffer heatSourceStagingBuffer;
    VkDeviceMemory heatSourceStagingMemory;
};