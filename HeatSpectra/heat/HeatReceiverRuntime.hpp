#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "mesh/remesher/SupportingHalfedge.hpp"

class VulkanDevice;
class MemoryAllocator;

class HeatReceiverRuntime {
public:
    HeatReceiverRuntime(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        uint32_t runtimeModelId,
        const SupportingHalfedge::IntrinsicMesh& intrinsicMesh,
        VkBufferView supportingHalfedgeView,
        VkBufferView supportingAngleView,
        VkBufferView halfedgeView,
        VkBufferView edgeView,
        VkBufferView triangleView,
        VkBufferView lengthView,
        VkBufferView inputHalfedgeView,
        VkBufferView inputEdgeView,
        VkBufferView inputTriangleView,
        VkBufferView inputLengthView);
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
        uint32_t nodeCount,
        bool forceReallocate = false);
    void executeBufferTransfers(VkCommandBuffer commandBuffer);

    void cleanup();
    void cleanupStagingBuffers();

    void setVoronoiMapping(VkBuffer mappingBuffer, VkDeviceSize mappingOffset);

    uint32_t getRuntimeModelId() const { return runtimeModelId; }

    size_t getIntrinsicVertexCount() const { return intrinsicMesh.vertices.size(); }
    const SupportingHalfedge::IntrinsicMesh& getIntrinsicMesh() const { return intrinsicMesh; }

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
    SupportingHalfedge::IntrinsicMesh intrinsicMesh{};
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
