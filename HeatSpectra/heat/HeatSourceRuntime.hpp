#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "heat/HeatGpuStructs.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"

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

    VkBuffer getSourceBuffer() const {
        return sourceBuffer;
    }
    VkDeviceSize getSourceBufferOffset() const {
        return sourceBufferOffset_;
    }
    VkBufferView getSourceBufferView() const {
        return sourceBufferView;
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
    const SupportingHalfedge::IntrinsicMesh& getIntrinsicMesh() const {
        return intrinsicMesh;
    }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    SupportingHalfedge::IntrinsicMesh intrinsicMesh{};
    CommandPool& renderCommandPool;
    size_t intrinsicVertexCount = 0;

    VkBuffer sourceBuffer = VK_NULL_HANDLE;
    VkDeviceSize sourceBufferOffset_ = 0;
    VkBufferView sourceBufferView = VK_NULL_HANDLE;

    VkBuffer triangleCentroidBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleCentroidBufferOffset_ = 0;
    uint32_t triangleCount_ = 0;
    bool initialized = false;
    float uniformTemperature = 100.0f;
    std::vector<heat::SurfacePoint> surfacePointsCache;
    std::vector<heat::SurfacePoint> triangleCentroidsCache;
};
