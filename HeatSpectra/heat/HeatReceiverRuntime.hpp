#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"

class VulkanDevice;
class MemoryAllocator;

class HeatReceiverRuntime {
public:
    HeatReceiverRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        uint32_t runtimeModelId,
        const GeometryData& geometryData,
        const IntrinsicMeshData& intrinsicMeshData,
        const RuntimeIntrinsicCache::Entry& intrinsicResources);
    ~HeatReceiverRuntime();

    bool createReceiverBuffers();
    bool initializeReceiverBuffer();
    bool resetSurfaceTemp();

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

    uint32_t getRuntimeModelId() const { return runtimeModelId; }

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
    VkBuffer getVoronoiMappingBuffer() const { return voronoiMappingBuffer; }
    VkDeviceSize getVoronoiMappingBufferOffset() const { return voronoiMappingBufferOffset; }

    VkDescriptorSet getSurfaceComputeSetA() const { return surfaceComputeSetA; }
    VkDescriptorSet getSurfaceComputeSetB() const { return surfaceComputeSetB; }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    uint32_t runtimeModelId = 0;
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
