#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;
class CommandPool;

class VoronoiModelRuntime {
public:
    VoronoiModelRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        Model& model,
        const GeometryData& geometryData,
        const IntrinsicMeshData& intrinsicMeshData,
        const RuntimeIntrinsicCache::Entry& intrinsicResources,
        CommandPool& renderCommandPool);
    ~VoronoiModelRuntime();

    bool createVoronoiBuffers();

    void stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices);
    void executeBufferTransfers(VkCommandBuffer commandBuffer);
    void cleanup();
    void cleanupStagingBuffers();

    Model& getModel() { return model; }
    const Model& getModel() const { return model; }
    uint32_t getNodeModelId() const { return nodeModelId; }
    uint32_t getRuntimeModelId() const { return runtimeModelId; }
    const GeometryData& getGeometryData() const { return geometryData; }
    const IntrinsicMeshData& getIntrinsicMeshData() const { return intrinsicMeshData; }

    size_t getIntrinsicVertexCount() const { return intrinsicVertexCount; }
    size_t getIntrinsicTriangleCount() const { return intrinsicTriangleCount; }
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
    Model& model;
    uint32_t nodeModelId = 0;
    uint32_t runtimeModelId = 0;
    GeometryData geometryData{};
    IntrinsicMeshData intrinsicMeshData{};
    const RuntimeIntrinsicCache::Entry* intrinsicResources = nullptr;
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
