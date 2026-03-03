#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "util/Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;
class Remesher;

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSource {
public:
    HeatSource(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& heatModel, Remesher& remesher, CommandPool& renderCommandPool);
    ~HeatSource();

    bool createSourceBuffer();
    void cleanup();
    bool isInitialized() const { return initialized; }

    // Getters
    size_t getVertexCount() const;
    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    bool hasSurfaceBuffer() const { return sourceBuffer != VK_NULL_HANDLE && getVertexCount() > 0; }
    bool hasTriangleBuffer() const { return triangleGeometryBuffer != VK_NULL_HANDLE && triangleCount_ > 0; }

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
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& heatModel;
    Remesher& remesher;
    CommandPool& renderCommandPool;  
    size_t intrinsicVertexCount = 0;      

    HeatSourcePushConstant heatSourcePushConstant;
    VkBuffer sourceBuffer = VK_NULL_HANDLE;
    VkDeviceSize sourceBufferOffset_ = 0;
    VkBufferView sourceBufferView = VK_NULL_HANDLE; 
    
    VkBuffer triangleGeometryBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleGeometryBufferOffset_ = 0;

    VkBuffer triangleCentroidBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleCentroidBufferOffset_ = 0;
    uint32_t triangleCount_ = 0;
    bool initialized = false;
};
