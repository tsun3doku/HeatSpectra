#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>

#include "voronoi/VoronoiDomainRuntime.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

class VulkanDevice;
class MemoryAllocator;
class CommandPool;
class VoronoiNodeDomain;
class VoxelGrid;

class VoronoiModelRuntime : public VoronoiDomainRuntime {
public:
    VoronoiModelRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        uint32_t runtimeModelId,
        const glm::mat4& modelMatrix,
        const std::vector<glm::vec3>& geometryPositions,
        const std::vector<uint32_t>& geometryTriangleIndices,
        const std::vector<voronoi::SurfaceVertex>& surfaceVertices,
        const std::vector<uint32_t>& surfaceTriangleIndices,
        CommandPool& renderCommandPool);
    ~VoronoiModelRuntime();

    bool isPointDomain() const override { return false; }
    bool createVoronoiBuffers() override;
    bool createSurfaceBuffers();
    bool resetSurfaceState();
    bool buildAndStageSurfaceMappings(VoronoiNodeDomain& nodeDomain, const VoxelGrid& voxelGrid);

    void cleanup() override;

    uint32_t getRuntimeModelId() const override { return runtimeModelId; }
    const glm::mat4& getModelMatrix() const { return modelMatrix; }

    size_t getSurfaceVertexCount() const { return surfaceVertices.size(); }
    size_t getSurfaceTriangleCount() const { return surfaceTriangleIndices.size() / 3; }
    const std::vector<glm::vec3>& getGeometryPositions() const { return geometryPositions; }
    const std::vector<uint32_t>& getGeometryTriangleIndices() const { return geometryTriangleIndices; }
    const std::vector<voronoi::SurfaceVertex>& getSurfaceVertices() const { return surfaceVertices; }
    const std::vector<uint32_t>& getSurfaceTriangleIndices() const { return surfaceTriangleIndices; }
    VkBuffer getTriangleIndicesBuffer() const override { return triangleIndicesBuffer; }
    VkDeviceSize getTriangleIndicesBufferOffset() const override { return triangleIndicesBufferOffset; }
    VkBuffer getCandidateBuffer() const override { return voronoiCandidateBuffer; }
    VkDeviceSize getCandidateBufferOffset() const override { return voronoiCandidateBufferOffset; }
    VkBuffer getSurfaceBuffer() const override { return surfaceBuffer; }
    VkDeviceSize getSurfaceBufferOffset() const override { return surfaceBufferOffset; }
    VkBuffer getGMLSSurfaceStencilBuffer() const override { return gmlsSurfaceStencilBuffer; }
    VkDeviceSize getGMLSSurfaceStencilBufferOffset() const override { return gmlsSurfaceStencilBufferOffset; }
    VkBuffer getGMLSSurfaceWeightBuffer() const override { return gmlsSurfaceWeightBuffer; }
    VkDeviceSize getGMLSSurfaceWeightBufferOffset() const override { return gmlsSurfaceWeightBufferOffset; }
    size_t getGMLSSurfaceWeightCount() const override { return valueWeightCount; }
    VkBuffer getGMLSSurfaceGradientWeightBuffer() const override { return gmlsSurfaceGradientWeightBuffer; }
    VkDeviceSize getGMLSSurfaceGradientWeightBufferOffset() const override { return gmlsSurfaceGradientWeightBufferOffset; }
    size_t getGMLSSurfaceGradientWeightCount() const override { return gradientWeightCount; }

private:
    bool stageGMLSSurfaceData(
        const std::vector<voronoi::GMLSSurfaceStencil>& stencils,
        const std::vector<voronoi::GMLSSurfaceWeight>& valueWeights,
        const std::vector<voronoi::GMLSSurfaceGradientWeight>& gradientWeights);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    uint32_t runtimeModelId = 0;
    glm::mat4 modelMatrix{1.0f};
    std::vector<glm::vec3> geometryPositions;
    std::vector<uint32_t> geometryTriangleIndices;
    std::vector<voronoi::SurfaceVertex> surfaceVertices;
    std::vector<uint32_t> surfaceTriangleIndices;
    CommandPool& renderCommandPool;

    size_t valueWeightCount = 0;
    size_t gradientWeightCount = 0;

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

    VkBuffer surfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceBufferOffset = 0;
};
