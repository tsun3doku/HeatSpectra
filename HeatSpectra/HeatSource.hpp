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

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSource {
public:
    HeatSource(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, ResourceManager& resourceManager, CommandPool& renderCommandPool);
    ~HeatSource();


    void createSourceBuffer();

    void controller(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, float deltaTime);

    void cleanup();

    // Getters
    size_t getVertexCount() const;
    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }

    VkBuffer getSourceBuffer() const {
        return sourceBuffer;
    }
    VkDeviceSize getSourceBufferOffset() const {
        return sourceBufferOffset_;
    }
    VkBufferView getSourceBufferView() const {
        return sourceBufferView;
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
    size_t intrinsicVertexCount = 0;      

    HeatSourcePushConstant heatSourcePushConstant;
    VkBuffer sourceBuffer;
    VkDeviceSize sourceBufferOffset_;
    VkBufferView sourceBufferView = VK_NULL_HANDLE; 
    
    VkBuffer triangleGeometryBuffer;
    VkDeviceSize triangleGeometryBufferOffset_;

    VkBuffer triangleCentroidBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleCentroidBufferOffset_ = 0;
    uint32_t triangleCount_ = 0;
};
