#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"

class VulkanDevice;
class MemoryAllocator;
class Model;

class VoronoiGeometryRuntime {
public:
    VoronoiGeometryRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        Model& model,
        const GeometryData& geometryData,
        const IntrinsicMeshData& intrinsicMeshData,
        const RuntimeIntrinsicCache::Entry& intrinsicResources);
    ~VoronoiGeometryRuntime();

    bool createSurfaceBuffers();
    bool initializeSurfaceBuffer();
    bool resetSurfaceState();

    void updateDescriptors(
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
    void recreateDescriptors(
        VkDescriptorSetLayout surfaceLayout,
        VkDescriptorPool surfacePool,
        VkBuffer tempBufferA,
        VkDeviceSize tempBufferAOffset,
        VkBuffer tempBufferB,
        VkDeviceSize tempBufferBOffset,
        VkBuffer timeBuffer,
        VkDeviceSize timeBufferOffset,
        uint32_t nodeCount);

    void cleanup();
    void cleanupStagingBuffers();

    void setVoronoiMapping(VkBuffer mappingBuffer, VkDeviceSize mappingOffset);

    Model& getModel() { return model; }
    const Model& getModel() const { return model; }

    size_t getIntrinsicVertexCount() const { return intrinsicMeshData.vertices.size(); }
    const GeometryData& getGeometryData() const { return geometryData; }
    const IntrinsicMeshData& getIntrinsicMeshData() const { return intrinsicMeshData; }

    VkBuffer getSurfaceBuffer() const { return surfaceBuffer; }
    VkDeviceSize getSurfaceBufferOffset() const { return surfaceBufferOffset; }
    VkBufferView getSurfaceBufferView() const { return surfaceBufferView; }
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

    VkBuffer getSurfaceVertexBuffer() const { return surfaceVertexBuffer; }
    VkDeviceSize getSurfaceVertexBufferOffset() const { return surfaceVertexBufferOffset; }

    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Model& model;
    GeometryData geometryData{};
    IntrinsicMeshData intrinsicMeshData{};
    const RuntimeIntrinsicCache::Entry* intrinsicResources = nullptr;

    static constexpr float AMBIENT_TEMPERATURE = 1.0f;

    VkBuffer voronoiMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiMappingBufferOffset = 0;

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
};
