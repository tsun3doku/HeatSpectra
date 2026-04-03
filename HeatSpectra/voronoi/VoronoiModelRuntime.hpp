#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "mesh/remesher/SupportingHalfedge.hpp"

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class VoronoiModelRuntime {
public:
    struct CpuData {
        uint32_t nodeModelId = 0;
        SupportingHalfedge::IntrinsicMesh intrinsicMesh;
        std::vector<glm::vec3> geometryPositions;
        std::vector<uint32_t> geometryTriangleIndices;
        std::vector<glm::vec3> intrinsicSurfacePositions;
        std::vector<uint32_t> intrinsicTriangleIndices;
    };

    VoronoiModelRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        uint32_t runtimeModelId,
        VkBuffer vertexBuffer,
        VkDeviceSize vertexBufferOffset,
        VkBuffer indexBuffer,
        VkDeviceSize indexBufferOffset,
        uint32_t indexCount,
        const glm::mat4& modelMatrix,
        CpuData cpuData,
        VkBufferView supportingHalfedgeView,
        VkBufferView supportingAngleView,
        VkBufferView halfedgeView,
        VkBufferView edgeView,
        VkBufferView triangleView,
        VkBufferView lengthView,
        VkBufferView inputHalfedgeView,
        VkBufferView inputEdgeView,
        VkBufferView inputTriangleView,
        VkBufferView inputLengthView,
        CommandPool& renderCommandPool);
    ~VoronoiModelRuntime();

    bool createVoronoiBuffers();

    void stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices);
    void executeBufferTransfers(VkCommandBuffer commandBuffer);
    void cleanup();
    void cleanupStagingBuffers();

    uint32_t getNodeModelId() const { return nodeModelId; }
    uint32_t getRuntimeModelId() const { return runtimeModelId; }
    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkDeviceSize getVertexBufferOffset() const { return vertexBufferOffset; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    VkDeviceSize getIndexBufferOffset() const { return indexBufferOffset; }
    uint32_t getIndexCount() const { return indexCount; }
    const glm::mat4& getModelMatrix() const { return modelMatrix; }

    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    size_t getIntrinsicTriangleCount() const { return intrinsicTriangleCount; }
    const SupportingHalfedge::IntrinsicMesh& getIntrinsicMesh() const { return intrinsicMesh; }
    const std::vector<glm::vec3>& getGeometryPositions() const { return geometryPositions; }
    const std::vector<uint32_t>& getGeometryTriangleIndices() const { return geometryTriangleIndices; }
    const std::vector<glm::vec3>& getIntrinsicSurfacePositions() const { return intrinsicSurfacePositions; }
    const std::vector<uint32_t>& getIntrinsicTriangleIndices() const { return intrinsicTriangleIndices; }
    const std::vector<uint32_t>& getVoronoiSurfaceCellIndices() const { return voronoiSurfaceCellIndices; }

    VkBuffer getTriangleIndicesBuffer() const { return triangleIndicesBuffer; }
    VkDeviceSize getTriangleIndicesBufferOffset() const { return triangleIndicesBufferOffset; }
    VkBuffer getVoronoiMappingBuffer() const { return voronoiMappingBuffer; }
    VkDeviceSize getVoronoiMappingBufferOffset() const { return voronoiMappingBufferOffset; }
    VkBuffer getVoronoiCandidateBuffer() const { return voronoiCandidateBuffer; }
    VkDeviceSize getVoronoiCandidateBufferOffset() const { return voronoiCandidateBufferOffset; }
    VkBufferView getSupportingHalfedgeView() const;
    VkBufferView getSupportingAngleView() const;
    VkBufferView getHalfedgeView() const;
    VkBufferView getEdgeView() const;
    VkBufferView getTriangleView() const;
    VkBufferView getLengthView() const;
    VkBufferView getInputHalfedgeView() const;
    VkBufferView getInputEdgeView() const;
    VkBufferView getInputTriangleView() const;
    VkBufferView getInputLengthView() const;

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    uint32_t nodeModelId = 0;
    uint32_t runtimeModelId = 0;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t indexCount = 0;
    glm::mat4 modelMatrix{1.0f};
    SupportingHalfedge::IntrinsicMesh intrinsicMesh;
    std::vector<glm::vec3> geometryPositions;
    std::vector<uint32_t> geometryTriangleIndices;
    VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
    VkBufferView supportingAngleView = VK_NULL_HANDLE;
    VkBufferView halfedgeView = VK_NULL_HANDLE;
    VkBufferView edgeView = VK_NULL_HANDLE;
    VkBufferView triangleView = VK_NULL_HANDLE;
    VkBufferView lengthView = VK_NULL_HANDLE;
    VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
    VkBufferView inputEdgeView = VK_NULL_HANDLE;
    VkBufferView inputTriangleView = VK_NULL_HANDLE;
    VkBufferView inputLengthView = VK_NULL_HANDLE;
    CommandPool& renderCommandPool;

    size_t intrinsicVertexCount = 0;
    size_t intrinsicTriangleCount = 0;
    std::vector<glm::vec3> intrinsicSurfacePositions;
    std::vector<uint32_t> intrinsicTriangleIndices;
    std::vector<uint32_t> voronoiSurfaceCellIndices;

    VkBuffer triangleIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleIndicesBufferOffset = 0;

    VkBuffer voronoiMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingBufferOffset = 0;

    VkBuffer voronoiMappingStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingStagingOffset = 0;
    VkDeviceSize voronoiMappingBufferSize = 0;

    VkBuffer voronoiCandidateBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiCandidateBufferOffset = 0;
};
