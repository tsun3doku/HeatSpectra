#pragma once

#include "domain/RemeshData.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "util/Structs.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

class RuntimeIntrinsicCache {
public:
    struct Entry {
        IntrinsicMeshData intrinsic;
        size_t triangleCount = 0;
        size_t vertexCount = 0;
        float averageTriangleArea = 0.0f;
        bool uploaded = false;

        VkBuffer bufferS = VK_NULL_HANDLE;
        VkBuffer bufferA = VK_NULL_HANDLE;
        VkBuffer bufferH = VK_NULL_HANDLE;
        VkBuffer bufferE = VK_NULL_HANDLE;
        VkBuffer bufferT = VK_NULL_HANDLE;
        VkBuffer bufferL = VK_NULL_HANDLE;
        VkBuffer bufferHInput = VK_NULL_HANDLE;
        VkBuffer bufferEInput = VK_NULL_HANDLE;
        VkBuffer bufferTInput = VK_NULL_HANDLE;
        VkBuffer bufferLInput = VK_NULL_HANDLE;
        VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
        VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;

        VkDeviceSize offsetS = 0;
        VkDeviceSize offsetA = 0;
        VkDeviceSize offsetH = 0;
        VkDeviceSize offsetE = 0;
        VkDeviceSize offsetT = 0;
        VkDeviceSize offsetL = 0;
        VkDeviceSize offsetHInput = 0;
        VkDeviceSize offsetEInput = 0;
        VkDeviceSize offsetTInput = 0;
        VkDeviceSize offsetLInput = 0;
        VkDeviceSize triangleGeometryOffset = 0;
        VkDeviceSize vertexGeometryOffset = 0;

        VkBufferView viewS = VK_NULL_HANDLE;
        VkBufferView viewA = VK_NULL_HANDLE;
        VkBufferView viewH = VK_NULL_HANDLE;
        VkBufferView viewE = VK_NULL_HANDLE;
        VkBufferView viewT = VK_NULL_HANDLE;
        VkBufferView viewL = VK_NULL_HANDLE;
        VkBufferView viewHInput = VK_NULL_HANDLE;
        VkBufferView viewEInput = VK_NULL_HANDLE;
        VkBufferView viewTInput = VK_NULL_HANDLE;
        VkBufferView viewLInput = VK_NULL_HANDLE;
    };

    RuntimeIntrinsicCache(VulkanDevice& vulkanDevice, MemoryAllocator& allocator)
        : vulkanDevice(vulkanDevice), allocator(allocator) {
    }

    ~RuntimeIntrinsicCache() {
        cleanup();
    }

    bool apply(const NodeDataHandle& intrinsicHandle, const IntrinsicMeshData& intrinsic) {
        if (intrinsicHandle.key == 0) {
            return false;
        }

        Entry& entry = entriesByHandle[intrinsicHandle];
        release(entry);
        entry.intrinsic = intrinsic;

        auto uploadTexel = [this](const void* data, VkDeviceSize size, VkFormat format,
            VkBuffer& buffer, VkDeviceSize& offset, VkBufferView& view, const char* label) -> bool {
                if (size == 0) {
                    return false;
                }
                if (createTexelBuffer(allocator, vulkanDevice, data, size, format, buffer, offset, view) != VK_SUCCESS) {
                    std::cerr << "[RuntimeIntrinsicCache] Failed to upload " << label << " buffer" << std::endl;
                    return false;
                }
                return true;
            };

        if (!uploadTexel(intrinsic.supportingHalfedges.data(), intrinsic.supportingHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, entry.bufferS, entry.offsetS, entry.viewS, "supporting-halfedge") ||
            !uploadTexel(intrinsic.supportingAngles.data(), intrinsic.supportingAngles.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, entry.bufferA, entry.offsetA, entry.viewA, "supporting-angle") ||
            !uploadTexel(intrinsic.intrinsicHalfedges.data(), intrinsic.intrinsicHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT, entry.bufferH, entry.offsetH, entry.viewH, "intrinsic-halfedge") ||
            !uploadTexel(intrinsic.intrinsicEdges.data(), intrinsic.intrinsicEdges.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT, entry.bufferE, entry.offsetE, entry.viewE, "intrinsic-edge") ||
            !uploadTexel(intrinsic.intrinsicTriangles.data(), intrinsic.intrinsicTriangles.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, entry.bufferT, entry.offsetT, entry.viewT, "intrinsic-triangle") ||
            !uploadTexel(intrinsic.intrinsicEdgeLengths.data(), intrinsic.intrinsicEdgeLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, entry.bufferL, entry.offsetL, entry.viewL, "intrinsic-length") ||
            !uploadTexel(intrinsic.inputHalfedges.data(), intrinsic.inputHalfedges.size() * sizeof(int32_t), VK_FORMAT_R32G32B32A32_SINT, entry.bufferHInput, entry.offsetHInput, entry.viewHInput, "input-halfedge") ||
            !uploadTexel(intrinsic.inputEdges.data(), intrinsic.inputEdges.size() * sizeof(int32_t), VK_FORMAT_R32G32_SINT, entry.bufferEInput, entry.offsetEInput, entry.viewEInput, "input-edge") ||
            !uploadTexel(intrinsic.inputTriangles.data(), intrinsic.inputTriangles.size() * sizeof(int32_t), VK_FORMAT_R32_SINT, entry.bufferTInput, entry.offsetTInput, entry.viewTInput, "input-triangle") ||
            !uploadTexel(intrinsic.inputEdgeLengths.data(), intrinsic.inputEdgeLengths.size() * sizeof(float), VK_FORMAT_R32_SFLOAT, entry.bufferLInput, entry.offsetLInput, entry.viewLInput, "input-length")) {
            release(entry);
            entriesByHandle.erase(intrinsicHandle);
            return false;
        }

        if (!intrinsic.triangles.empty()) {
            std::vector<IntrinsicTriangleData> gpuTriangles;
            gpuTriangles.reserve(intrinsic.triangles.size());
            for (const IntrinsicMeshTriangleData& triangle : intrinsic.triangles) {
                IntrinsicTriangleData gpuTriangle{};
                gpuTriangle.center = glm::vec3(triangle.center[0], triangle.center[1], triangle.center[2]);
                gpuTriangle.area = triangle.area;
                gpuTriangle.normal = glm::vec3(triangle.normal[0], triangle.normal[1], triangle.normal[2]);
                gpuTriangle.padding = 0.0f;
                gpuTriangles.push_back(gpuTriangle);
            }

            void* mappedPtr = nullptr;
            if (createStorageBuffer(
                    allocator,
                    vulkanDevice,
                    gpuTriangles.data(),
                    gpuTriangles.size() * sizeof(IntrinsicTriangleData),
                    entry.intrinsicTriangleBuffer,
                    entry.triangleGeometryOffset,
                    &mappedPtr) != VK_SUCCESS) {
                std::cerr << "[RuntimeIntrinsicCache] Failed to upload intrinsic triangle geometry" << std::endl;
                release(entry);
                entriesByHandle.erase(intrinsicHandle);
                return false;
            }
        }

        if (!intrinsic.vertices.empty()) {
            std::vector<IntrinsicVertexData> gpuVertices;
            gpuVertices.reserve(intrinsic.vertices.size());
            for (const IntrinsicMeshVertexData& vertex : intrinsic.vertices) {
                IntrinsicVertexData gpuVertex{};
                gpuVertex.position = glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]);
                gpuVertex.intrinsicVertexId = vertex.intrinsicVertexId;
                gpuVertex.normal = glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                gpuVertex.padding = 0.0f;
                gpuVertices.push_back(gpuVertex);
            }

            void* mappedPtr = nullptr;
            if (createStorageBuffer(
                    allocator,
                    vulkanDevice,
                    gpuVertices.data(),
                    gpuVertices.size() * sizeof(IntrinsicVertexData),
                    entry.intrinsicVertexBuffer,
                    entry.vertexGeometryOffset,
                    &mappedPtr) != VK_SUCCESS) {
                std::cerr << "[RuntimeIntrinsicCache] Failed to upload intrinsic vertex geometry" << std::endl;
                release(entry);
                entriesByHandle.erase(intrinsicHandle);
                return false;
            }
        }

        entry.triangleCount = intrinsic.triangles.size();
        entry.vertexCount = intrinsic.vertices.size();
        entry.averageTriangleArea = 0.0f;
        for (const IntrinsicMeshTriangleData& triangle : intrinsic.triangles) {
            entry.averageTriangleArea += triangle.area;
        }
        if (entry.triangleCount > 0) {
            entry.averageTriangleArea /= static_cast<float>(entry.triangleCount);
        }
        entry.uploaded = true;
        return true;
    }

    void remove(const NodeDataHandle& intrinsicHandle) {
        if (intrinsicHandle.key == 0) {
            return;
        }

        auto it = entriesByHandle.find(intrinsicHandle);
        if (it == entriesByHandle.end()) {
            return;
        }
        release(it->second);
        entriesByHandle.erase(it);
    }

    const Entry* get(const NodeDataHandle& intrinsicHandle) const {
        if (intrinsicHandle.key == 0) {
            return nullptr;
        }
        auto it = entriesByHandle.find(intrinsicHandle);
        if (it == entriesByHandle.end() || !it->second.uploaded) {
            return nullptr;
        }
        return &it->second;
    }

    bool isApplied(const NodeDataHandle& intrinsicHandle) const {
        return get(intrinsicHandle) != nullptr;
    }

    std::vector<NodeDataHandle> handles() const {
        std::vector<NodeDataHandle> result;
        result.reserve(entriesByHandle.size());
        for (const auto& [intrinsicHandle, entry] : entriesByHandle) {
            if (entry.uploaded) {
                result.push_back(intrinsicHandle);
            }
        }
        return result;
    }

    void cleanup() {
        for (auto& [intrinsicHandle, entry] : entriesByHandle) {
            (void)intrinsicHandle;
            release(entry);
        }
        entriesByHandle.clear();
    }

private:
    void release(Entry& entry) {
        auto destroyView = [this](VkBufferView& view) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyBufferView(vulkanDevice.getDevice(), view, nullptr);
                view = VK_NULL_HANDLE;
            }
        };
        destroyView(entry.viewS);
        destroyView(entry.viewA);
        destroyView(entry.viewH);
        destroyView(entry.viewE);
        destroyView(entry.viewT);
        destroyView(entry.viewL);
        destroyView(entry.viewHInput);
        destroyView(entry.viewEInput);
        destroyView(entry.viewTInput);
        destroyView(entry.viewLInput);

        auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
            if (buffer != VK_NULL_HANDLE) {
                allocator.free(buffer, offset);
                buffer = VK_NULL_HANDLE;
                offset = 0;
            }
        };
        freeBuffer(entry.bufferS, entry.offsetS);
        freeBuffer(entry.bufferA, entry.offsetA);
        freeBuffer(entry.bufferH, entry.offsetH);
        freeBuffer(entry.bufferE, entry.offsetE);
        freeBuffer(entry.bufferT, entry.offsetT);
        freeBuffer(entry.bufferL, entry.offsetL);
        freeBuffer(entry.bufferHInput, entry.offsetHInput);
        freeBuffer(entry.bufferEInput, entry.offsetEInput);
        freeBuffer(entry.bufferTInput, entry.offsetTInput);
        freeBuffer(entry.bufferLInput, entry.offsetLInput);
        freeBuffer(entry.intrinsicTriangleBuffer, entry.triangleGeometryOffset);
        freeBuffer(entry.intrinsicVertexBuffer, entry.vertexGeometryOffset);

        entry = {};
    }

    VulkanDevice& vulkanDevice;
    MemoryAllocator& allocator;
    std::map<NodeDataHandle, Entry> entriesByHandle;
};
