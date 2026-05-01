#include "HeatReceiverRuntime.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "util/GeometryUtils.hpp"
#include "util/Structs.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

HeatReceiverRuntime::HeatReceiverRuntime(
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
    VkBufferView inputLengthView)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      runtimeModelId(runtimeModelId),
      intrinsicMesh(intrinsicMesh),
      supportingHalfedgeView(supportingHalfedgeView),
      supportingAngleView(supportingAngleView),
      halfedgeView(halfedgeView),
      edgeView(edgeView),
      triangleView(triangleView),
      lengthView(lengthView),
      inputHalfedgeView(inputHalfedgeView),
      inputEdgeView(inputEdgeView),
      inputTriangleView(inputTriangleView),
      inputLengthView(inputLengthView) {
}

HeatReceiverRuntime::~HeatReceiverRuntime() {
}

bool HeatReceiverRuntime::createReceiverBuffers() {
    if (intrinsicMesh.vertices.empty()) {
        std::cerr << "[HeatReceiverRuntime] Missing intrinsic state for model" << std::endl;
        return false;
    }
    const size_t vertexCount = intrinsicMesh.vertices.size();
    if (vertexCount == 0) {
        std::cerr << "[HeatReceiverRuntime] Model has 0 intrinsic vertices" << std::endl;
        return false;
    }

    const VkDeviceSize vertexBufferSize = sizeof(heat::SurfacePoint) * vertexCount;
    if (createTexelBuffer(
            memoryAllocator,
            vulkanDevice,
            nullptr,
            vertexBufferSize,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            surfaceBuffer,
            surfaceBufferOffset,
            surfaceBufferView,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            16) != VK_SUCCESS) {
        std::cerr << "[HeatReceiverRuntime] Failed to create surface texel buffer" << std::endl;
        cleanup();
        return false;
    }

    if (createVertexBuffer(
            memoryAllocator,
            vertexBufferSize,
            surfaceVertexBuffer,
            surfaceVertexBufferOffset) != VK_SUCCESS) {
        std::cerr << "[HeatReceiverRuntime] Failed to create surface vertex buffer" << std::endl;
        cleanup();
        return false;
    }

    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = intrinsicMesh.vertices[i].position;
    }
    const std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicMesh.indices);

    std::vector<heat::SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        surfacePoints[i].position = positions[i];
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
        surfacePoints[i].area = vertexAreas[i];
        surfacePoints[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
        initStagingOffset = 0;
        initBufferSize = 0;
    }
    initBufferSize = sizeof(heat::SurfacePoint) * vertexCount;
    void* initStagingData = nullptr;
    if (createStagingBuffer(
            memoryAllocator,
            initBufferSize,
            initStagingBuffer,
            initStagingOffset,
            &initStagingData) != VK_SUCCESS ||
        !initStagingData) {
        std::cerr << "[HeatReceiverRuntime] Failed to create initialization staging buffer" << std::endl;
        cleanup();
        return false;
    }
    std::memcpy(initStagingData, surfacePoints.data(), static_cast<size_t>(initBufferSize));
    return true;
}

bool HeatReceiverRuntime::initializeReceiverBuffer() {
    if (initStagingBuffer != VK_NULL_HANDLE && initBufferSize > 0) {
        return true;
    }

    if (intrinsicMesh.vertices.empty()) {
        std::cerr << "[HeatReceiverRuntime] Missing intrinsic state for model" << std::endl;
        return false;
    }
    const size_t vertexCount = intrinsicMesh.vertices.size();
    if (vertexCount == 0 || vertexCount != getIntrinsicVertexCount()) {
        std::cerr << "[HeatReceiverRuntime] Vertex count mismatch or zero vertices." << std::endl;
        return false;
    }

    const VkDeviceSize bufferSize = sizeof(heat::SurfacePoint) * vertexCount;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* stagingData = nullptr;
    if (createStagingBuffer(
            memoryAllocator,
            bufferSize,
            stagingBuffer,
            stagingOffset,
            &stagingData) != VK_SUCCESS ||
        !stagingData) {
        std::cerr << "[HeatReceiverRuntime] Failed to create receiver staging buffer" << std::endl;
        return false;
    }

    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = intrinsicMesh.vertices[i].position;
    }
    const std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicMesh.indices);

    std::vector<heat::SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        surfacePoints[i].position = positions[i];
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
        surfacePoints[i].area = vertexAreas[i];
        surfacePoints[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    std::memcpy(stagingData, surfacePoints.data(), static_cast<size_t>(bufferSize));

    initStagingBuffer = stagingBuffer;
    initStagingOffset = stagingOffset;
    initBufferSize = bufferSize;
    return true;
}

bool HeatReceiverRuntime::resetSurfaceTemp() {
    cleanupStagingBuffers();
    return initializeReceiverBuffer();
}

void HeatReceiverRuntime::setGMLSSurfaceWeights(
    VkBuffer stencilBuffer,
    VkDeviceSize stencilBufferOffset,
    VkBuffer valueWeightBuffer,
    VkDeviceSize valueWeightBufferOffset,
    VkBuffer gradientWeightBuffer,
    VkDeviceSize gradientWeightBufferOffset) {
    gmlsSurfaceStencilBuffer = stencilBuffer;
    gmlsSurfaceStencilBufferOffset = stencilBufferOffset;
    gmlsSurfaceWeightBuffer = valueWeightBuffer;
    gmlsSurfaceWeightBufferOffset = valueWeightBufferOffset;
    gmlsSurfaceGradientWeightBuffer = gradientWeightBuffer;
    gmlsSurfaceGradientWeightBufferOffset = gradientWeightBufferOffset;
}

void HeatReceiverRuntime::updateDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    VkBuffer tempBufferA,
    VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB,
    VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer,
    VkDeviceSize timeBufferOffset,
    uint32_t nodeCount,
    bool forceReallocate) {
    const size_t intrinsicVertexCount = getIntrinsicVertexCount();
    if (intrinsicVertexCount == 0 ||
        gmlsSurfaceStencilBuffer == VK_NULL_HANDLE ||
        gmlsSurfaceWeightBuffer == VK_NULL_HANDLE ||
        gmlsSurfaceGradientWeightBuffer == VK_NULL_HANDLE) {
        return;
    }

    if (forceReallocate) {
        surfaceComputeSetA = VK_NULL_HANDLE;
        surfaceComputeSetB = VK_NULL_HANDLE;
    }

    if (surfaceComputeSetA == VK_NULL_HANDLE || surfaceComputeSetB == VK_NULL_HANDLE) {
        std::vector<VkDescriptorSetLayout> layouts = { surfaceLayout, surfaceLayout };

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = surfacePool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts.data();

        VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, sets) != VK_SUCCESS) {
            std::cerr << "[HeatReceiverRuntime] Failed to allocate surface ping-pong descriptor sets" << std::endl;
            return;
        }

        surfaceComputeSetA = sets[0];
        surfaceComputeSetB = sets[1];
    }

    VkDescriptorBufferInfo surfaceInfo{ surfaceBuffer, surfaceBufferOffset, sizeof(heat::SurfacePoint) * intrinsicVertexCount };
    VkDescriptorBufferInfo timeInfo{ timeBuffer, timeBufferOffset, sizeof(heat::TimeUniform) };
    VkDescriptorBufferInfo gmlsStencilInfo{
        gmlsSurfaceStencilBuffer,
        gmlsSurfaceStencilBufferOffset,
        VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo gmlsValueWeightInfo{
        gmlsSurfaceWeightBuffer,
        gmlsSurfaceWeightBufferOffset,
        VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo gmlsGradientWeightInfo{
        gmlsSurfaceGradientWeightBuffer,
        gmlsSurfaceGradientWeightBufferOffset,
        VK_WHOLE_SIZE
    };

    const VkDescriptorSet sets[2] = { surfaceComputeSetA, surfaceComputeSetB };
    const VkBuffer tempBuffers[2] = { tempBufferA, tempBufferB };
    const VkDeviceSize tempOffsets[2] = { tempBufferAOffset, tempBufferBOffset };

    for (uint32_t pass = 0; pass < 2; ++pass) {
        VkDescriptorBufferInfo nodeTempInfo{ tempBuffers[pass], tempOffsets[pass], sizeof(float) * nodeCount };
        std::array<VkDescriptorBufferInfo*, 6> infos = {
            &nodeTempInfo,
            &surfaceInfo,
            &timeInfo,
            &gmlsStencilInfo,
            &gmlsValueWeightInfo,
            &gmlsGradientWeightInfo
        };
        std::array<uint32_t, 6> bindings = { 0u, 1u, 3u, 10u, 11u, 12u };
        std::array<VkDescriptorType, 6> types = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        };
        std::array<VkWriteDescriptorSet, 6> writes{};
        for (size_t i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = sets[pass];
            writes[i].dstBinding = bindings[i];
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = types[i];
            writes[i].pBufferInfo = infos[i];
        }
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

}

void HeatReceiverRuntime::executeBufferTransfers(VkCommandBuffer commandBuffer) {
    if (initStagingBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkBufferCopy surfaceCopyRegion{ initStagingOffset, surfaceBufferOffset, initBufferSize };
    vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceBuffer, 1, &surfaceCopyRegion);

    VkBufferCopy vertexCopyRegion{ initStagingOffset, surfaceVertexBufferOffset, initBufferSize };
    vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceVertexBuffer, 1, &vertexCopyRegion);
}

void HeatReceiverRuntime::cleanup() {
    if (surfaceBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), surfaceBufferView, nullptr);
        surfaceBufferView = VK_NULL_HANDLE;
    }
    if (surfaceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceBuffer, surfaceBufferOffset);
        surfaceBuffer = VK_NULL_HANDLE;
        surfaceBufferOffset = 0;
    }
    if (surfaceVertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceVertexBuffer, surfaceVertexBufferOffset);
        surfaceVertexBuffer = VK_NULL_HANDLE;
        surfaceVertexBufferOffset = 0;
    }

    gmlsSurfaceStencilBuffer = VK_NULL_HANDLE;
    gmlsSurfaceStencilBufferOffset = 0;
    gmlsSurfaceWeightBuffer = VK_NULL_HANDLE;
    gmlsSurfaceWeightBufferOffset = 0;
    gmlsSurfaceGradientWeightBuffer = VK_NULL_HANDLE;
    gmlsSurfaceGradientWeightBufferOffset = 0;

    cleanupStagingBuffers();
}

VkBufferView HeatReceiverRuntime::getSupportingHalfedgeView() const {
    return supportingHalfedgeView;
}

VkBufferView HeatReceiverRuntime::getSupportingAngleView() const {
    return supportingAngleView;
}

VkBufferView HeatReceiverRuntime::getHalfedgeView() const {
    return halfedgeView;
}

VkBufferView HeatReceiverRuntime::getEdgeView() const {
    return edgeView;
}

VkBufferView HeatReceiverRuntime::getTriangleView() const {
    return triangleView;
}

VkBufferView HeatReceiverRuntime::getLengthView() const {
    return lengthView;
}

VkBufferView HeatReceiverRuntime::getInputHalfedgeView() const {
    return inputHalfedgeView;
}

VkBufferView HeatReceiverRuntime::getInputEdgeView() const {
    return inputEdgeView;
}

VkBufferView HeatReceiverRuntime::getInputTriangleView() const {
    return inputTriangleView;
}

VkBufferView HeatReceiverRuntime::getInputLengthView() const {
    return inputLengthView;
}

void HeatReceiverRuntime::cleanupStagingBuffers() {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
        initStagingOffset = 0;
        initBufferSize = 0;
    }
}
