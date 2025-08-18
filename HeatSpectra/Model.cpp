#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtx/norm.hpp>
#include <algorithm>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "CommandBufferManager.hpp"
#include "Camera.hpp"
#include "AABBTree.hpp"
#include "Model.hpp"

void Model::init(const std::string modelPath) {
    loadModel(modelPath);
  
    createVertexBuffer();
    createIndexBuffer();

    createSurfaceBuffer();
}

Model::Model(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), camera(camera) {
}

Model::~Model() {
    cleanup();
}

void Model::recreateBuffers() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    size_t oldIndexCount = indices.size();
    size_t oldVertCount = vertices.size();
    VkBuffer   oldVB = vertexBuffer;
    VkBuffer   oldIB = indexBuffer;
    VkDeviceSize oldOffset = indexBufferOffset_;

    std::cout << "*** recreateBuffers() before cleanup ***\n"
        << "    old verts:  " << oldVertCount << ", old indices: " << oldIndexCount << "\n"
        << "    old VB handle: " << oldVB << ", old IB handle: " << oldIB << "\n";

    cleanup();

    // remeshing should have updated `vertices` and `indices` already...
    std::cout << "*** recreateBuffers() creating new buffers ***\n"
        << "    new verts:  " << vertices.size() << ", new indices: " << indices.size() << "\n";

    createVertexBuffer();
    createIndexBuffer();
    createSurfaceBuffer();

    std::cout << "*** recreateBuffers() after creation ***\n"
        << "    new VB handle: " << vertexBuffer
        << "  offset: " << vertexBufferOffset_ << "\n"
        << "    new IB handle: " << indexBuffer
        << "  offset: " << indexBufferOffset_ << "\n";
}

std::array<glm::vec3, 8> Model::calculateBoundingBox(const std::vector<Vertex>& vertices, glm::vec3& minBound, glm::vec3& maxBound) {
    // Initialize min and max bounding box values
    minBound = glm::vec3(FLT_MAX);
    maxBound = glm::vec3(-FLT_MAX);

    // Iterate through all vertices to find the min and max coordinates
    for (const auto& vertex : vertices) {
        minBound.x = std::min(minBound.x, vertex.pos.x);
        minBound.y = std::min(minBound.y, vertex.pos.y);
        minBound.z = std::min(minBound.z, vertex.pos.z);

        maxBound.x = std::max(maxBound.x, vertex.pos.x);
        maxBound.y = std::max(maxBound.y, vertex.pos.y);
        maxBound.z = std::max(maxBound.z, vertex.pos.z);
    }

    std::array<glm::vec3, 8> points;
    points[0] = minBound; // min x, min y, min z
    points[1] = glm::vec3(maxBound.x, minBound.y, minBound.z); 
    points[2] = glm::vec3(maxBound.x, maxBound.y, minBound.z); 
    points[3] = glm::vec3(minBound.x, maxBound.y, minBound.z); 
    points[4] = glm::vec3(minBound.x, minBound.y, maxBound.z); 
    points[5] = glm::vec3(maxBound.x, minBound.y, maxBound.z); 
    points[6] = maxBound; // max x, max y, max z
    points[7] = glm::vec3(minBound.x, maxBound.y, maxBound.z); 

    return points;
}

glm::vec3 Model::getBoundingBoxCenter() {
    glm::vec3 minBound, maxBound;
    std::array<glm::vec3, 8> points = calculateBoundingBox(vertices, minBound, maxBound);

    return (points[0] + points[1] + points[2] + points[3] +
        points[4] + points[5] + points[6] + points[7]) * 0.125f; // Calculate the average
}

void Model::loadModel(const std::string& modelPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str())) {
        throw std::runtime_error(warn + err);
    }

    // Create vertices directly from OBJ vertex list (preserving order)
    size_t vertexCount = attrib.vertices.size() / 3;
    vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i].pos = {
            attrib.vertices[3 * i + 0],
            attrib.vertices[3 * i + 1],
            attrib.vertices[3 * i + 2]
        };
        vertices[i].color = { 1.0f, 1.0f, 1.0f };
        // Set default normal (will be recalculated later)
        vertices[i].normal = { 0.0f, 0.0f, 1.0f };
        vertices[i].texCoord = { 0.0f, 0.0f }; // Default UV
    }

    // Process faces and build indices
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            // Use the original vertex index directly
            indices.push_back(index.vertex_index);

            // Update texture coordinates if available
            if (index.texcoord_index >= 0 && index.texcoord_index < attrib.texcoords.size() / 2) {
                vertices[index.vertex_index].texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }
        }
    }

    modelPosition = getBoundingBoxCenter();
}

void Model::buildAABBTree() {
    aabbTree = std::make_unique<AABBTree>(*this);
    aabbTree->build();
}

void Model::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    std::cout << "[BUFFER] createVertexBuffer(): uploading "
        << vertices.size() << " verts\n";

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, vertices.data(), static_cast<size_t>(bufferSize));

    // Allocate the local device vertex buffer
    auto [vertexBufferHandle, vertexBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
        vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
    );

    // Copy from the staging buffer to the vertex buffer
    copyBuffer(vulkanDevice, stagingBuffer, stagingOffset, vertexBufferHandle, vertexBufferOffset, bufferSize);

    // Free the staging buffer 
    memoryAllocator.free(stagingBuffer, stagingOffset);

    // Assign handles and offsets
    vertexBuffer = vertexBufferHandle;
    vertexBufferOffset_ = vertexBufferOffset; 
}

void Model::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    std::cout << "[BUFFER] createIndexBuffer(): uploading "
        << indices.size() << " indices\n";

    // Allocate staging buffer using MemoryAllocator
    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, indices.data(), static_cast<size_t>(bufferSize));

    // Allocate device-local index buffer using MemoryAllocator
    auto [indexBufferHandle, indexBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );

    // Copy data with offsets
    copyBuffer(vulkanDevice, stagingBuffer, stagingOffset, indexBufferHandle, indexBufferOffset, bufferSize);

    // Free staging buffer
    memoryAllocator.free(stagingBuffer, stagingOffset);

    // Assign handles and offsets
    indexBuffer = indexBufferHandle;
    indexBufferOffset_ = indexBufferOffset;
}

void Model::createSurfaceBuffer() {
    VkDeviceSize bufferSize = sizeof(SurfaceVertex) * vertices.size();

    // Allocate surfaceBuffer
    auto [surfaceBufferHandle, surfaceBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    surfaceBuffer = surfaceBufferHandle;
    surfaceBufferOffset_ = surfaceBufferOffset;

    // Allocate surfaceVertexBuffer 
    auto [surfaceVertexBufferHandle, surfaceVertexBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    surfaceVertexBuffer = surfaceVertexBufferHandle;
    surfaceVertexBufferOffset_ = surfaceVertexBufferOffset;
}

glm::vec3 Model::getFaceNormal(uint32_t faceIndex) const {
    // Calculate face normal from the triangle vertices
    uint32_t i0 = indices[faceIndex * 3];
    uint32_t i1 = indices[faceIndex * 3 + 1];
    uint32_t i2 = indices[faceIndex * 3 + 2];

    glm::vec3 v0 = vertices[i0].pos;
    glm::vec3 v1 = vertices[i1].pos;
    glm::vec3 v2 = vertices[i2].pos;

    return glm::normalize(glm::cross(v1 - v0, v2 - v0));
}

void Model::equalizeFaceAreas() {
    std::vector<glm::vec3> centroids(indices.size() / 3);

    // Calculate face centroids
    for (size_t i = 0; i < indices.size(); i += 3) {
        centroids[i / 3] = (vertices[indices[i]].pos +
            vertices[indices[i + 1]].pos +
            vertices[indices[i + 2]].pos) / 3.0f;
    }

    // Adjust vertices toward centroids (mild relaxation)
    const float relaxation = 0.15f; // [0 = no change, 0.5 = full]
    std::vector<glm::vec3> newPositions(vertices.size(), glm::vec3(0));
    std::vector<int> vertexCounts(vertices.size(), 0);

    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 centroid = centroids[i / 3];
        for (int j = 0; j < 3; j++) {
            uint32_t idx = indices[i + j];
            newPositions[idx] += centroid;
            vertexCounts[idx]++;
        }
    }

    for (size_t i = 0; i < vertices.size(); i++) {
        if (vertexCounts[i] > 0) {
            vertices[i].pos = glm::mix(vertices[i].pos,
                newPositions[i] / float(vertexCounts[i]),
                relaxation);
        }
    }
}

void Model::weldVertices(float epsilon) {
    std::vector<Vertex> newVertices;
    std::unordered_map<glm::vec3, uint32_t, Vec3Hash> posMap;
    std::vector<uint32_t> oldToNew(vertices.size()); 

    auto quantize = [epsilon](float val) {
        return std::round(val / epsilon) * epsilon;
        };

    // Create new vertices and build index map
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        glm::vec3 qpos = {
            quantize(v.pos.x),
            quantize(v.pos.y),
            quantize(v.pos.z)
        };

        if (auto it = posMap.find(qpos); it != posMap.end()) {
            oldToNew[i] = it->second;
        }
        else {
            uint32_t newIndex = static_cast<uint32_t>(newVertices.size());
            posMap[qpos] = newIndex;
            oldToNew[i] = newIndex;
            newVertices.push_back(v);
        }
    }

    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());
    for (auto oldIndex : indices) {
        newIndices.push_back(oldToNew[oldIndex]);
    }

    vertices = std::move(newVertices);
    indices = std::move(newIndices);
}

void Model::recalculateNormals() {
    // Reset all normals
    for (auto& vertex : vertices) {
        vertex.normal = glm::vec3(0.0f);
    }

    // Edge angle threshold for sharp edges in radians
    float sharpAngleThreshold = 20.0f * (3.14159f / 180.0f);

    // Create a map to track which faces contribute to which vertex normals
    std::unordered_map<uint32_t, std::vector<glm::vec3>> vertexFaceNormals;

    // Calculate face normals and accumulate
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        glm::vec3 v0 = vertices[i0].pos;
        glm::vec3 v1 = vertices[i1].pos;
        glm::vec3 v2 = vertices[i2].pos;

        glm::vec3 faceNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

        // Store face normal for each vertex
        vertexFaceNormals[i0].push_back(faceNormal);
        vertexFaceNormals[i1].push_back(faceNormal);
        vertexFaceNormals[i2].push_back(faceNormal);
    }

    // Process each vertex to determine if its on a sharp edge
    for (auto& pair : vertexFaceNormals) {
        uint32_t vertexIndex = pair.first;
        std::vector<glm::vec3>& faceNormals = pair.second;

        // Check if this vertex is on a sharp edge by looking at face normal angles
        bool hasSharpEdge = false;
        for (size_t i = 0; i < faceNormals.size(); i++) {
            for (size_t j = i + 1; j < faceNormals.size(); j++) {
                float angle = glm::acos(glm::clamp(glm::dot(faceNormals[i], faceNormals[j]), -1.0f, 1.0f));
                if (angle > sharpAngleThreshold) {
                    hasSharpEdge = true;
                    break;
                }
            }
            if (hasSharpEdge) break;
        }

        if (hasSharpEdge) {
            // For vertices on sharp edges, group similar facing normals
            std::vector<glm::vec3> normalGroups;
            std::vector<int> normalGroupCounts;

            for (const auto& faceNormal : faceNormals) {
                bool foundGroup = false;
                for (size_t i = 0; i < normalGroups.size(); i++) {
                    float angle = glm::acos(glm::clamp(glm::dot(normalGroups[i], faceNormal), -1.0f, 1.0f));
                    if (angle < sharpAngleThreshold) {
                        // Add to existing group
                        normalGroups[i] = (normalGroups[i] * float(normalGroupCounts[i]) + faceNormal) / float(normalGroupCounts[i] + 1);
                        normalGroupCounts[i]++;
                        foundGroup = true;
                        break;
                    }
                }

                if (!foundGroup) {
                    // Create new group
                    normalGroups.push_back(faceNormal);
                    normalGroupCounts.push_back(1);
                }
            }

            // Find the dominant normal group
            int maxCount = 0;
            glm::vec3 dominantNormal(0.0f);
            for (size_t i = 0; i < normalGroups.size(); i++) {
                if (normalGroupCounts[i] > maxCount) {
                    maxCount = normalGroupCounts[i];
                    dominantNormal = normalGroups[i];
                }
            }

            vertices[vertexIndex].normal = glm::normalize(dominantNormal);
        }
        else {
            // For smooth vertices, average all face normals
            glm::vec3 avgNormal(0.0f);
            for (const auto& normal : faceNormals) {
                avgNormal += normal;
            }
            vertices[vertexIndex].normal = glm::normalize(avgNormal);
        }
    }
}

void Model::updateGeometry(const std::vector<Vertex>& newVertices, const std::vector<uint32_t>& newIndices) {
    vertices = newVertices;
    indices = newIndices;

    updateVertexBuffer();
    updateIndexBuffer();
}

void Model::updateVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // Create a staging buffer
    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Copy data to staging buffer
    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, vertices.data(), static_cast<size_t>(bufferSize));

    // Copy from staging buffer to vertex buffer
    copyBuffer(vulkanDevice, stagingBuffer, stagingOffset, vertexBuffer, vertexBufferOffset_, bufferSize);

    // Free staging buffer
    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void Model::updateIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // Create a staging buffer
    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Copy data to staging buffer
    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, indices.data(), static_cast<size_t>(bufferSize));

    // Copy from staging buffer to index buffer
    copyBuffer(vulkanDevice, stagingBuffer, stagingOffset, indexBuffer, indexBufferOffset_, bufferSize);

    // Free staging buffer
    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void Model::saveOBJ(const std::string& path) const {
    std::ofstream out(path);
    // write vertices
    for (auto& v : vertices)
        out << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z << "\n";
    // write faces (1-based indices)
    for (size_t i = 0; i < indices.size(); i += 3)
        out << "f "
        << indices[i] + 1 << " "
        << indices[i + 1] + 1 << " "
        << indices[i + 2] + 1 << "\n";
}

HitResult Model::rayIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    const float EPSILON = 0.0000001f;
    HitResult result{ false, std::numeric_limits<float>::max(), 0, {0, 0, 0} };

    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 v0 = vertices[indices[i]].pos;
        glm::vec3 v1 = vertices[indices[i + 1]].pos;
        glm::vec3 v2 = vertices[indices[i + 2]].pos;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(rayDir, edge2);
        float a = glm::dot(edge1, h);

        if (a > -EPSILON && a < EPSILON)
            continue;

        float f = 1.0f / a;
        glm::vec3 s = rayOrigin - v0;
        float u = f * glm::dot(s, h);

        if (u < 0.0f || u > 1.0f)
            continue;

        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(rayDir, q);

        if (v < 0.0f || u + v > 1.0f)
            continue;

        float t = f * glm::dot(edge2, q);

        if (t > EPSILON && t < result.distance) {
            result.hit = true;
            result.distance = t;
            result.vertexIndices[0] = indices[i];
            result.vertexIndices[1] = indices[i + 1];
            result.vertexIndices[2] = indices[i + 2];
        }
    }

    return result;
}

void Model::markEdge(uint32_t triIndex, int edgeNum) {
    uint32_t v0 = indices[triIndex + edgeNum];
    uint32_t v1 = indices[triIndex + (edgeNum + 1) % 3];
    featuredEdges.insert(Edge(std::min(v0, v1), std::max(v0, v1)));

    vertices[v0].color = glm::vec3(1, 0, 0);
    vertices[v1].color = glm::vec3(1, 0, 0);
}

void Model::cleanup() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(vertexBuffer, vertexBufferOffset_);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(indexBuffer, indexBufferOffset_);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (surfaceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceBuffer, surfaceBufferOffset_);
        surfaceBuffer = VK_NULL_HANDLE;
    }
    if (surfaceVertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceVertexBuffer, surfaceVertexBufferOffset_);
        surfaceVertexBuffer = VK_NULL_HANDLE;
    }
}