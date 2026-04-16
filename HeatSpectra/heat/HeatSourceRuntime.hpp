#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "mesh/remesher/SupportingHalfedge.hpp"
#include "util/Structs.hpp"

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

const std::string HEATSOURCE_PATH = "models/heatsource_torus.obj";

class HeatSourceRuntime {
public:
    HeatSourceRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
        CommandPool& renderCommandPool,
        float initialTemperature);
    ~HeatSourceRuntime();

    bool createSourceBuffer();
    void cleanup();
    bool isInitialized() const { return initialized; }
    void setUniformTemperature(float temperature);
    float getUniformTemperature() const { return uniformTemperature; }

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

    void setHeatSourcePushConstant(glm::mat4 modelMatrix) {
        heatSourcePushConstant.heatSourceModelMatrix = modelMatrix;
    }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    SupportingHalfedge::IntrinsicMesh intrinsicMesh{};
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
    float uniformTemperature = 100.0f;
    std::vector<SurfacePoint> surfacePointsCache;
    std::vector<SurfacePoint> triangleCentroidsCache;
};
