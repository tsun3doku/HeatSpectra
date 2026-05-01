#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "mesh/remesher/SupportingHalfedge.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class VoronoiModelRuntime {
public:
    struct SurfaceVertex {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 0.0f, 1.0f};
    };

    struct CpuData {
        uint32_t nodeModelId = 0;
        SupportingHalfedge::IntrinsicMesh intrinsicMesh;
        std::vector<glm::vec3> geometryPositions;
        std::vector<uint32_t> geometryTriangleIndices;
        std::vector<SurfaceVertex> surfaceVertices;
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
    bool createSurfaceBuffers();
    bool initializeSurfaceBuffer();
    bool resetSurfaceState();

    void stageGMLSSurfaceData(
        const std::vector<voronoi::GMLSSurfaceStencil>& stencils,
        const std::vector<voronoi::GMLSSurfaceWeight>& valueWeights,
        const std::vector<voronoi::GMLSSurfaceGradientWeight>& gradientWeights);
    void updateSurfaceDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t nodeCount);
    void recreateSurfaceDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t nodeCount);
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
    std::vector<glm::vec3> getIntrinsicSurfacePositions() const;
    const std::vector<uint32_t>& getIntrinsicTriangleIndices() const { return intrinsicTriangleIndices; }
    VkBuffer getTriangleIndicesBuffer() const { return triangleIndicesBuffer; }
    VkDeviceSize getTriangleIndicesBufferOffset() const { return triangleIndicesBufferOffset; }
    VkBuffer getVoronoiCandidateBuffer() const { return voronoiCandidateBuffer; }
    VkDeviceSize getVoronoiCandidateBufferOffset() const { return voronoiCandidateBufferOffset; }
    VkBuffer getSurfaceBuffer() const { return surfaceBuffer; }
    VkDeviceSize getSurfaceBufferOffset() const { return surfaceBufferOffset; }
    VkBufferView getSurfaceBufferView() const { return surfaceBufferView; }
    VkBuffer getSurfaceVertexBuffer() const { return surfaceVertexBuffer; }
    VkDeviceSize getSurfaceVertexBufferOffset() const { return surfaceVertexBufferOffset; }
    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }
    VkBuffer getGMLSSurfaceStencilBuffer() const { return gmlsSurfaceStencilBuffer; }
    VkDeviceSize getGMLSSurfaceStencilBufferOffset() const { return gmlsSurfaceStencilBufferOffset; }
    VkBuffer getGMLSSurfaceWeightBuffer() const { return gmlsSurfaceWeightBuffer; }
    VkDeviceSize getGMLSSurfaceWeightBufferOffset() const { return gmlsSurfaceWeightBufferOffset; }
    VkBuffer getGMLSSurfaceGradientWeightBuffer() const { return gmlsSurfaceGradientWeightBuffer; }
    VkDeviceSize getGMLSSurfaceGradientWeightBufferOffset() const { return gmlsSurfaceGradientWeightBufferOffset; }
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
    std::vector<SurfaceVertex> surfaceVertices;
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
    std::vector<uint32_t> intrinsicTriangleIndices;

    VkBuffer triangleIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize triangleIndicesBufferOffset = 0;

    VkBuffer voronoiCandidateBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiCandidateBufferOffset = 0;

    VkBuffer gmlsSurfaceStencilBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceStencilBufferOffset = 0;

    VkBuffer gmlsSurfaceWeightBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceWeightBufferOffset = 0;

    VkBuffer gmlsSurfaceGradientWeightBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceGradientWeightBufferOffset = 0;

    VkBuffer gmlsSurfaceStencilStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceStencilStagingOffset = 0;
    VkDeviceSize gmlsSurfaceStencilBufferSize = 0;

    VkBuffer gmlsSurfaceWeightStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceWeightStagingOffset = 0;
    VkDeviceSize gmlsSurfaceWeightBufferSize = 0;

    VkBuffer gmlsSurfaceGradientWeightStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsSurfaceGradientWeightStagingOffset = 0;
    VkDeviceSize gmlsSurfaceGradientWeightBufferSize = 0;

    VkBuffer surfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceBufferOffset = 0;
    VkBufferView surfaceBufferView = VK_NULL_HANDLE;

    VkBuffer surfaceVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceVertexBufferOffset = 0;

    VkDescriptorSet surfaceComputeSetA = VK_NULL_HANDLE;
    VkDescriptorSet surfaceComputeSetB = VK_NULL_HANDLE;

    VkBuffer initStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize initStagingOffset = 0;
    VkDeviceSize initBufferSize = 0;

    static constexpr float AMBIENT_TEMPERATURE = 1.0f;
};
