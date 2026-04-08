#include "Remesher.hpp"

#include "iODT.hpp"
#include "util/Structs.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>

Remesher::Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator) {
}

SupportingHalfedge::GPUResources Remesher::buildIntrinsicGPUResources(
    const SupportingHalfedge& supportingHalfedge,
    const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) const {
    const SupportingHalfedge::GPUBuffers gpuBuffers = supportingHalfedge.buildGPUBuffers();

    SupportingHalfedge::GPUResources resources{};
    resources.triangleCount = intrinsicMesh.triangles.size();
    resources.vertexCount = intrinsicMesh.vertices.size();
    resources.averageTriangleArea = computeAverageTriangleArea(intrinsicMesh);

    auto uploadTexel = [this, &resources](const void* data, VkDeviceSize size, VkFormat format,
        VkBuffer& buffer, VkDeviceSize& offset, VkBufferView& view, const char* label) -> bool {
            if (createTexelBuffer(memoryAllocator, vulkanDevice, data, size, format, buffer, offset, view) != VK_SUCCESS) {
                std::cerr << "[Remesher] Failed to upload " << label << " buffer" << std::endl;
                cleanupGpuResources(resources);
                return false;
            }
            return true;
        };

    if (!uploadTexel(gpuBuffers.supportingHalfedges.data(), gpuBuffers.supportingHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32_SINT,
        resources.bufferS, resources.offsetS, resources.viewS, "S")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.supportingAngles.data(), gpuBuffers.supportingAngles.size() * sizeof(float), VK_FORMAT_R32_SFLOAT,
        resources.bufferA, resources.offsetA, resources.viewA, "A")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.intrinsicHalfedgeData.data(), gpuBuffers.intrinsicHalfedgeData.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT,
        resources.bufferH, resources.offsetH, resources.viewH, "H")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.intrinsicEdgeData.data(), gpuBuffers.intrinsicEdgeData.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT,
        resources.bufferE, resources.offsetE, resources.viewE, "E")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.intrinsicTriangleData.data(), gpuBuffers.intrinsicTriangleData.size() * sizeof(int32_t), VK_FORMAT_R32_SINT,
        resources.bufferT, resources.offsetT, resources.viewT, "T")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.intrinsicLengths.data(), gpuBuffers.intrinsicLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT,
        resources.bufferL, resources.offsetL, resources.viewL, "L")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.inputHalfedgeData.data(), gpuBuffers.inputHalfedgeData.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT,
        resources.bufferHInput, resources.offsetHInput, resources.viewHInput, "H_input")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.inputEdgeData.data(), gpuBuffers.inputEdgeData.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT,
        resources.bufferEInput, resources.offsetEInput, resources.viewEInput, "E_input")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.inputTriangleData.data(), gpuBuffers.inputTriangleData.size() * sizeof(int32_t), VK_FORMAT_R32_SINT,
        resources.bufferTInput, resources.offsetTInput, resources.viewTInput, "T_input")) {
        return {};
    }
    if (!uploadTexel(gpuBuffers.inputLengths.data(), gpuBuffers.inputLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT,
        resources.bufferLInput, resources.offsetLInput, resources.viewLInput, "L_input")) {
        return {};
    }

    if (!intrinsicMesh.triangles.empty()) {
        std::vector<IntrinsicTriangleData> gpuTriangles;
        gpuTriangles.reserve(intrinsicMesh.triangles.size());
        for (const SupportingHalfedge::IntrinsicTriangle& triangle : intrinsicMesh.triangles) {
            IntrinsicTriangleData gpuTriangle{};
            gpuTriangle.center = triangle.center;
            gpuTriangle.normal = triangle.normal;
            gpuTriangle.area = triangle.area;
            gpuTriangle.padding = 0.0f;
            gpuTriangles.push_back(gpuTriangle);
        }

        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                memoryAllocator,
                vulkanDevice,
                gpuTriangles.data(),
                gpuTriangles.size() * sizeof(IntrinsicTriangleData),
                resources.intrinsicTriangleBuffer,
                resources.triangleGeometryOffset,
                &mappedPtr) != VK_SUCCESS) {
            std::cerr << "[Remesher] Failed to upload intrinsic triangle data" << std::endl;
            cleanupGpuResources(resources);
            return {};
        }
    }

    if (!intrinsicMesh.vertices.empty()) {
        std::vector<IntrinsicVertexData> gpuVertices;
        gpuVertices.reserve(intrinsicMesh.vertices.size());
        for (const SupportingHalfedge::IntrinsicVertex& vertex : intrinsicMesh.vertices) {
            IntrinsicVertexData gpuVertex{};
            gpuVertex.position = vertex.position;
            gpuVertex.intrinsicVertexId = vertex.intrinsicVertexId;
            gpuVertex.normal = vertex.normal;
            gpuVertex.padding = 0.0f;
            gpuVertices.push_back(gpuVertex);
        }

        void* mappedPtr = nullptr;
        if (createStorageBuffer(
                memoryAllocator,
                vulkanDevice,
                gpuVertices.data(),
                gpuVertices.size() * sizeof(IntrinsicVertexData),
                resources.intrinsicVertexBuffer,
                resources.vertexGeometryOffset,
                &mappedPtr) != VK_SUCCESS) {
            std::cerr << "[Remesher] Failed to upload intrinsic vertex data" << std::endl;
            cleanupGpuResources(resources);
            return {};
        }
    }

    return resources;
}

bool Remesher::remesh(
    const std::vector<float>& pointPositions,
    const std::vector<uint32_t>& triangleIndices,
    int iterations,
    double minAngleDegrees,
    double maxEdgeLength,
    double stepSize,
    RemeshResult& outResult) const {
    outResult = {};
    if (pointPositions.empty() || triangleIndices.empty()) {
        std::cerr << "[Remesher] Cannot remesh empty geometry input" << std::endl;
        return false;
    }

    iODT remesher(pointPositions, triangleIndices);
    const bool success = remesher.optimalDelaunayTriangulation(
        iterations,
        minAngleDegrees,
        maxEdgeLength,
        stepSize);
    if (!success) {
        std::cerr << "[Remesher] Payload remeshing failed" << std::endl;
        return false;
    }

    SupportingHalfedge* supportingHalfedge = remesher.getSupportingHalfedge();
    if (!supportingHalfedge) {
        std::cerr << "[Remesher] Missing supporting halfedge after remesh" << std::endl;
        return false;
    }

    const SupportingHalfedge::IntrinsicMesh intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
    if (intrinsicMesh.vertices.empty() || intrinsicMesh.indices.empty()) {
        std::cerr << "[Remesher] Intrinsic mesh output was empty" << std::endl;
        return false;
    }

    outResult.intrinsicMesh = intrinsicMesh;
    outResult.intrinsicGpuResources = buildIntrinsicGPUResources(*supportingHalfedge, outResult.intrinsicMesh);
    remesher.cleanup();
    return true;
}

void Remesher::cleanupGpuResources(SupportingHalfedge::GPUResources& resources) const {
    auto destroyView = [this](VkBufferView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyBufferView(vulkanDevice.getDevice(), view, nullptr);
            view = VK_NULL_HANDLE;
        }
    };
    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    destroyView(resources.viewS);
    destroyView(resources.viewA);
    destroyView(resources.viewH);
    destroyView(resources.viewE);
    destroyView(resources.viewT);
    destroyView(resources.viewL);
    destroyView(resources.viewHInput);
    destroyView(resources.viewEInput);
    destroyView(resources.viewTInput);
    destroyView(resources.viewLInput);

    freeBuffer(resources.bufferS, resources.offsetS);
    freeBuffer(resources.bufferA, resources.offsetA);
    freeBuffer(resources.bufferH, resources.offsetH);
    freeBuffer(resources.bufferE, resources.offsetE);
    freeBuffer(resources.bufferT, resources.offsetT);
    freeBuffer(resources.bufferL, resources.offsetL);
    freeBuffer(resources.bufferHInput, resources.offsetHInput);
    freeBuffer(resources.bufferEInput, resources.offsetEInput);
    freeBuffer(resources.bufferTInput, resources.offsetTInput);
    freeBuffer(resources.bufferLInput, resources.offsetLInput);
    freeBuffer(resources.intrinsicTriangleBuffer, resources.triangleGeometryOffset);
    freeBuffer(resources.intrinsicVertexBuffer, resources.vertexGeometryOffset);

    resources = {};
}

float Remesher::computeAverageTriangleArea(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) const {
    if (intrinsicMesh.triangles.empty()) {
        return 0.0f;
    }

    float totalArea = 0.0f;
    for (const SupportingHalfedge::IntrinsicTriangle& triangle : intrinsicMesh.triangles) {
        totalArea += triangle.area;
    }
    return totalArea / static_cast<float>(intrinsicMesh.triangles.size());
}
