#include "VoronoiModelRuntime.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "mesh/remesher/iODT.hpp"
#include "util/GeometryUtils.hpp"
#include "util/Structs.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

VoronoiModelRuntime::VoronoiModelRuntime(
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
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      nodeModelId(cpuData.nodeModelId),
      runtimeModelId(runtimeModelId),
      vertexBuffer(vertexBuffer),
      vertexBufferOffset(vertexBufferOffset),
      indexBuffer(indexBuffer),
      indexBufferOffset(indexBufferOffset),
      indexCount(indexCount),
      modelMatrix(modelMatrix),
      intrinsicMesh(std::move(cpuData.intrinsicMesh)),
      geometryPositions(std::move(cpuData.geometryPositions)),
      geometryTriangleIndices(std::move(cpuData.geometryTriangleIndices)),
      surfaceVertices(std::move(cpuData.surfaceVertices)),
      intrinsicTriangleIndices(std::move(cpuData.intrinsicTriangleIndices)),
      supportingHalfedgeView(supportingHalfedgeView),
      supportingAngleView(supportingAngleView),
      halfedgeView(halfedgeView),
      edgeView(edgeView),
      triangleView(triangleView),
      lengthView(lengthView),
      inputHalfedgeView(inputHalfedgeView),
      inputEdgeView(inputEdgeView),
      inputTriangleView(inputTriangleView),
      inputLengthView(inputLengthView),
      renderCommandPool(renderCommandPool) {
}

VoronoiModelRuntime::~VoronoiModelRuntime() {
}

bool VoronoiModelRuntime::createVoronoiBuffers() {
    if (intrinsicMesh.vertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing intrinsic state for model" << std::endl;
        return false;
    }

    const size_t vertexCount = intrinsicMesh.vertices.size();
    const size_t triangleCount = intrinsicMesh.indices.size() / 3;
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 intrinsic vertices" << std::endl;
        return false;
    }

    intrinsicVertexCount = vertexCount;
    intrinsicTriangleCount = triangleCount;

    constexpr uint32_t K_CANDIDATES = 64;
    const VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * intrinsicTriangleIndices.size();
    const VkDeviceSize candidateBufferSize = sizeof(uint32_t) * triangleCount * K_CANDIDATES;
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
        triangleIndicesBufferOffset = 0;
    }
    auto [triIdxHandle, triIdxOffset] = memoryAllocator.allocate(
        triangleIndicesBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        storageAlignment);
    if (triIdxHandle == VK_NULL_HANDLE) {
        std::cerr << "[VoronoiModelRuntime] Failed to allocate triangle index buffer" << std::endl;
        cleanup();
        return false;
    }
    triangleIndicesBuffer = triIdxHandle;
    triangleIndicesBufferOffset = triIdxOffset;

    VkBuffer triIdxStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize triIdxStagingOffset = 0;
    void* triIdxStagingData = nullptr;
    if (createStagingBuffer(
            memoryAllocator,
            triangleIndicesBufferSize,
            triIdxStagingBuffer,
            triIdxStagingOffset,
            &triIdxStagingData) != VK_SUCCESS ||
        !triIdxStagingData) {
        std::cerr << "[VoronoiModelRuntime] Failed to create triangle index staging buffer" << std::endl;
        cleanup();
        return false;
    }
    std::memcpy(triIdxStagingData, intrinsicTriangleIndices.data(), static_cast<size_t>(triangleIndicesBufferSize));

    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }

    VkBuffer candidateStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize candidateStagingOffset = 0;
    if (candidateBufferSize > 0) {
        auto [candidateHandle, candidateOffset] = memoryAllocator.allocate(
            candidateBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            storageAlignment);
        if (candidateHandle == VK_NULL_HANDLE) {
            std::cerr << "[VoronoiModelRuntime] Failed to allocate Voronoi candidate buffer" << std::endl;
            memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
            cleanup();
            return false;
        }
        voronoiCandidateBuffer = candidateHandle;
        voronoiCandidateBufferOffset = candidateOffset;

        std::vector<uint32_t> candidateInitData(static_cast<size_t>(triangleCount) * K_CANDIDATES, 0xFFFFFFFFu);
        void* candidateStagingData = nullptr;
        if (createStagingBuffer(
                memoryAllocator,
                candidateBufferSize,
                candidateStagingBuffer,
                candidateStagingOffset,
                &candidateStagingData) != VK_SUCCESS ||
            !candidateStagingData) {
            std::cerr << "[VoronoiModelRuntime] Failed to create Voronoi candidate staging buffer" << std::endl;
            memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
            cleanup();
            return false;
        }
        std::memcpy(candidateStagingData, candidateInitData.data(), static_cast<size_t>(candidateBufferSize));
    }

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    {
        VkBufferCopy region{};
        region.srcOffset = triIdxStagingOffset;
        region.dstOffset = triangleIndicesBufferOffset;
        region.size = triangleIndicesBufferSize;
        vkCmdCopyBuffer(cmd, triIdxStagingBuffer, triangleIndicesBuffer, 1, &region);
    }
    if (candidateStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy region{};
        region.srcOffset = candidateStagingOffset;
        region.dstOffset = voronoiCandidateBufferOffset;
        region.size = candidateBufferSize;
        vkCmdCopyBuffer(cmd, candidateStagingBuffer, voronoiCandidateBuffer, 1, &region);
    }
    renderCommandPool.endCommands(cmd);

    memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
    if (candidateStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(candidateStagingBuffer, candidateStagingOffset);
    }

    return true;
}

bool VoronoiModelRuntime::createSurfaceBuffers() {
    if (surfaceVertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing surface state for model" << std::endl;
        return false;
    }
    const size_t vertexCount = surfaceVertices.size();
    if (vertexCount == 0) {
        std::cerr << "[VoronoiModelRuntime] Model has 0 surface vertices" << std::endl;
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
        std::cerr << "[VoronoiModelRuntime] Failed to create surface texel buffer" << std::endl;
        cleanup();
        return false;
    }

    if (createVertexBuffer(
            memoryAllocator,
            vertexBufferSize,
            surfaceVertexBuffer,
            surfaceVertexBufferOffset) != VK_SUCCESS) {
        std::cerr << "[VoronoiModelRuntime] Failed to create surface vertex buffer" << std::endl;
        cleanup();
        return false;
    }

    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = surfaceVertices[i].position;
    }
    const std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicTriangleIndices);

    std::vector<heat::SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        surfacePoints[i].position = positions[i];
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = surfaceVertices[i].normal;
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
        std::cerr << "[VoronoiModelRuntime] Failed to create surface initialization staging buffer" << std::endl;
        cleanup();
        return false;
    }
    std::memcpy(initStagingData, surfacePoints.data(), static_cast<size_t>(initBufferSize));
    return true;
}

bool VoronoiModelRuntime::initializeSurfaceBuffer() {
    if (initStagingBuffer != VK_NULL_HANDLE && initBufferSize > 0) {
        return true;
    }

    if (surfaceVertices.empty()) {
        std::cerr << "[VoronoiModelRuntime] Missing surface state for model" << std::endl;
        return false;
    }
    const size_t vertexCount = surfaceVertices.size();
    if (vertexCount == 0 || vertexCount != getIntrinsicVertexCount()) {
        std::cerr << "[VoronoiModelRuntime] Surface vertex count mismatch or zero vertices." << std::endl;
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
        std::cerr << "[VoronoiModelRuntime] Failed to create surface staging buffer" << std::endl;
        return false;
    }

    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = surfaceVertices[i].position;
    }
    const std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicTriangleIndices);

    std::vector<heat::SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        surfacePoints[i].position = positions[i];
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = surfaceVertices[i].normal;
        surfacePoints[i].area = vertexAreas[i];
        surfacePoints[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    std::memcpy(stagingData, surfacePoints.data(), static_cast<size_t>(bufferSize));

    initStagingBuffer = stagingBuffer;
    initStagingOffset = stagingOffset;
    initBufferSize = bufferSize;
    return true;
}

bool VoronoiModelRuntime::resetSurfaceState() {
    cleanupStagingBuffers();
    return initializeSurfaceBuffer();
}

void VoronoiModelRuntime::updateSurfaceDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    VkBuffer tempBufferA,
    VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB,
    VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer,
    VkDeviceSize timeBufferOffset,
    uint32_t nodeCount) {
    const size_t intrinsicVertexCount = getIntrinsicVertexCount();
    if (intrinsicVertexCount == 0 ||
        gmlsSurfaceStencilBuffer == VK_NULL_HANDLE ||
        gmlsSurfaceWeightBuffer == VK_NULL_HANDLE ||
        gmlsSurfaceGradientWeightBuffer == VK_NULL_HANDLE) {
        return;
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
            std::cerr << "[VoronoiModelRuntime] Failed to allocate surface ping-pong descriptor sets" << std::endl;
            return;
        }

        surfaceComputeSetA = sets[0];
        surfaceComputeSetB = sets[1];
    }

    VkDescriptorBufferInfo surfaceInfo{ surfaceBuffer, surfaceBufferOffset, sizeof(heat::SurfacePoint) * intrinsicVertexCount };
    VkDescriptorBufferInfo timeInfo{ timeBuffer, timeBufferOffset, sizeof(heat::TimeUniform) };
    VkDescriptorBufferInfo stencilInfo{ gmlsSurfaceStencilBuffer, gmlsSurfaceStencilBufferOffset, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo weightInfo{ gmlsSurfaceWeightBuffer, gmlsSurfaceWeightBufferOffset, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo gradientWeightInfo{
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
            &stencilInfo,
            &weightInfo,
            &gradientWeightInfo
        };
        std::array<uint32_t, 6> bindings = { 0u, 1u, 3u, 10u, 11u, 12u };
        std::array<VkDescriptorType, 6> types = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
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

void VoronoiModelRuntime::recreateSurfaceDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    VkBuffer tempBufferA,
    VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB,
    VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer,
    VkDeviceSize timeBufferOffset,
    uint32_t nodeCount) {
    surfaceComputeSetA = VK_NULL_HANDLE;
    surfaceComputeSetB = VK_NULL_HANDLE;

    updateSurfaceDescriptors(
        surfaceLayout,
        surfacePool,
        tempBufferA,
        tempBufferAOffset,
        tempBufferB,
        tempBufferBOffset,
        timeBuffer,
        timeBufferOffset,
        nodeCount);
}

void VoronoiModelRuntime::stageGMLSSurfaceData(
    const std::vector<voronoi::GMLSSurfaceStencil>& stencils,
    const std::vector<voronoi::GMLSSurfaceWeight>& valueWeights,
    const std::vector<voronoi::GMLSSurfaceGradientWeight>& gradientWeights) {
    if (stencils.empty() || intrinsicVertexCount == 0 || stencils.size() != intrinsicVertexCount) {
        return;
    }

    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };
    auto freeStagingBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset, VkDeviceSize& size) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
        size = 0;
    };
    auto stageBuffer = [this, &freeBuffer, &freeStagingBuffer](
                           const void* srcData,
                           VkDeviceSize bufferSize,
                           VkBuffer& deviceBuffer,
                           VkDeviceSize& deviceOffset,
                           VkBuffer& stagingBuffer,
                           VkDeviceSize& stagingOffset,
                           VkDeviceSize& stagedSize) -> bool {
        freeStagingBuffer(stagingBuffer, stagingOffset, stagedSize);
        freeBuffer(deviceBuffer, deviceOffset);

        if (bufferSize == 0 || srcData == nullptr) {
            return true;
        }

        if (createStorageBuffer(
                memoryAllocator,
                vulkanDevice,
                nullptr,
                bufferSize,
                deviceBuffer,
                deviceOffset,
                nullptr,
                false,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT) != VK_SUCCESS ||
            deviceBuffer == VK_NULL_HANDLE) {
            std::cerr << "[VoronoiModelRuntime] Failed to allocate GMLS surface device buffer" << std::endl;
            return false;
        }

        void* stagingData = nullptr;
        if (createStagingBuffer(
                memoryAllocator,
                bufferSize,
                stagingBuffer,
                stagingOffset,
                &stagingData) != VK_SUCCESS ||
            !stagingData) {
            std::cerr << "[VoronoiModelRuntime] Failed to create GMLS surface staging buffer" << std::endl;
            freeBuffer(deviceBuffer, deviceOffset);
            return false;
        }

        std::memcpy(stagingData, srcData, static_cast<size_t>(bufferSize));
        stagedSize = bufferSize;
        return true;
    };

    if (!stageBuffer(
            stencils.data(),
            sizeof(voronoi::GMLSSurfaceStencil) * stencils.size(),
            gmlsSurfaceStencilBuffer,
            gmlsSurfaceStencilBufferOffset,
            gmlsSurfaceStencilStagingBuffer,
            gmlsSurfaceStencilStagingOffset,
            gmlsSurfaceStencilBufferSize)) {
        return;
    }
    if (!stageBuffer(
            valueWeights.data(),
            sizeof(voronoi::GMLSSurfaceWeight) * valueWeights.size(),
            gmlsSurfaceWeightBuffer,
            gmlsSurfaceWeightBufferOffset,
            gmlsSurfaceWeightStagingBuffer,
            gmlsSurfaceWeightStagingOffset,
            gmlsSurfaceWeightBufferSize)) {
        return;
    }
    if (!stageBuffer(
            gradientWeights.data(),
            sizeof(voronoi::GMLSSurfaceGradientWeight) * gradientWeights.size(),
            gmlsSurfaceGradientWeightBuffer,
            gmlsSurfaceGradientWeightBufferOffset,
            gmlsSurfaceGradientWeightStagingBuffer,
            gmlsSurfaceGradientWeightStagingOffset,
            gmlsSurfaceGradientWeightBufferSize)) {
        return;
    }
}

void VoronoiModelRuntime::executeBufferTransfers(VkCommandBuffer commandBuffer) {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy surfaceCopyRegion{ initStagingOffset, surfaceBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceBuffer, 1, &surfaceCopyRegion);

        VkBufferCopy vertexCopyRegion{ initStagingOffset, surfaceVertexBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceVertexBuffer, 1, &vertexCopyRegion);
    }

    if (gmlsSurfaceStencilStagingBuffer != VK_NULL_HANDLE && gmlsSurfaceStencilBuffer != VK_NULL_HANDLE) {
        VkBufferCopy copyRegion{
            gmlsSurfaceStencilStagingOffset,
            gmlsSurfaceStencilBufferOffset,
            gmlsSurfaceStencilBufferSize
        };
        vkCmdCopyBuffer(commandBuffer, gmlsSurfaceStencilStagingBuffer, gmlsSurfaceStencilBuffer, 1, &copyRegion);
    }
    if (gmlsSurfaceWeightStagingBuffer != VK_NULL_HANDLE && gmlsSurfaceWeightBuffer != VK_NULL_HANDLE) {
        VkBufferCopy copyRegion{
            gmlsSurfaceWeightStagingOffset,
            gmlsSurfaceWeightBufferOffset,
            gmlsSurfaceWeightBufferSize
        };
        vkCmdCopyBuffer(commandBuffer, gmlsSurfaceWeightStagingBuffer, gmlsSurfaceWeightBuffer, 1, &copyRegion);
    }
    if (gmlsSurfaceGradientWeightStagingBuffer != VK_NULL_HANDLE && gmlsSurfaceGradientWeightBuffer != VK_NULL_HANDLE) {
        VkBufferCopy copyRegion{
            gmlsSurfaceGradientWeightStagingOffset,
            gmlsSurfaceGradientWeightBufferOffset,
            gmlsSurfaceGradientWeightBufferSize
        };
        vkCmdCopyBuffer(commandBuffer, gmlsSurfaceGradientWeightStagingBuffer, gmlsSurfaceGradientWeightBuffer, 1, &copyRegion);
    }
}

void VoronoiModelRuntime::cleanup() {
    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
        triangleIndicesBufferOffset = 0;
    }
    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }
    if (gmlsSurfaceStencilBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceStencilBuffer, gmlsSurfaceStencilBufferOffset);
        gmlsSurfaceStencilBuffer = VK_NULL_HANDLE;
        gmlsSurfaceStencilBufferOffset = 0;
    }
    if (gmlsSurfaceWeightBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceWeightBuffer, gmlsSurfaceWeightBufferOffset);
        gmlsSurfaceWeightBuffer = VK_NULL_HANDLE;
        gmlsSurfaceWeightBufferOffset = 0;
    }
    if (gmlsSurfaceGradientWeightBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceGradientWeightBuffer, gmlsSurfaceGradientWeightBufferOffset);
        gmlsSurfaceGradientWeightBuffer = VK_NULL_HANDLE;
        gmlsSurfaceGradientWeightBufferOffset = 0;
    }

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

    cleanupStagingBuffers();
}

void VoronoiModelRuntime::cleanupStagingBuffers() {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
        initStagingOffset = 0;
        initBufferSize = 0;
    }

    if (gmlsSurfaceStencilStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceStencilStagingBuffer, gmlsSurfaceStencilStagingOffset);
        gmlsSurfaceStencilStagingBuffer = VK_NULL_HANDLE;
        gmlsSurfaceStencilStagingOffset = 0;
        gmlsSurfaceStencilBufferSize = 0;
    }
    if (gmlsSurfaceWeightStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceWeightStagingBuffer, gmlsSurfaceWeightStagingOffset);
        gmlsSurfaceWeightStagingBuffer = VK_NULL_HANDLE;
        gmlsSurfaceWeightStagingOffset = 0;
        gmlsSurfaceWeightBufferSize = 0;
    }
    if (gmlsSurfaceGradientWeightStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(gmlsSurfaceGradientWeightStagingBuffer, gmlsSurfaceGradientWeightStagingOffset);
        gmlsSurfaceGradientWeightStagingBuffer = VK_NULL_HANDLE;
        gmlsSurfaceGradientWeightStagingOffset = 0;
        gmlsSurfaceGradientWeightBufferSize = 0;
    }
}

std::vector<glm::vec3> VoronoiModelRuntime::getIntrinsicSurfacePositions() const {
    std::vector<glm::vec3> positions;
    positions.reserve(surfaceVertices.size());
    for (const SurfaceVertex& vertex : surfaceVertices) {
        positions.push_back(vertex.position);
    }
    return positions;
}

VkBufferView VoronoiModelRuntime::getSupportingHalfedgeView() const {
    return supportingHalfedgeView;
}

VkBufferView VoronoiModelRuntime::getSupportingAngleView() const {
    return supportingAngleView;
}

VkBufferView VoronoiModelRuntime::getHalfedgeView() const {
    return halfedgeView;
}

VkBufferView VoronoiModelRuntime::getEdgeView() const {
    return edgeView;
}

VkBufferView VoronoiModelRuntime::getTriangleView() const {
    return triangleView;
}

VkBufferView VoronoiModelRuntime::getLengthView() const {
    return lengthView;
}

VkBufferView VoronoiModelRuntime::getInputHalfedgeView() const {
    return inputHalfedgeView;
}

VkBufferView VoronoiModelRuntime::getInputEdgeView() const {
    return inputEdgeView;
}

VkBufferView VoronoiModelRuntime::getInputTriangleView() const {
    return inputTriangleView;
}

VkBufferView VoronoiModelRuntime::getInputLengthView() const {
    return inputLengthView;
}
