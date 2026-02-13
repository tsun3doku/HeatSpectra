#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <array>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "CommandBufferManager.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "Structs.hpp"

void Model::init(const std::string modelPath) {
    loadModel(modelPath);

    createVertexBuffer();
    createIndexBuffer();
    createRenderVertexBuffer();
    createRenderIndexBuffer();
}

Model::Model(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), camera(camera), commandPool(commandPool) {
}

Model::~Model() {
    cleanup();
}

void Model::recreateBuffers() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    cleanup();

    createVertexBuffer();
    createIndexBuffer();
    createRenderVertexBuffer();
    createRenderIndexBuffer();

    std::cout << "*** recreateBuffers() after creation ***\n"
        << "    new VB handle: " << vertexBuffer
        << "  offset: " << vertexBufferOffset_ << "\n"
        << "    new IB handle: " << indexBuffer
        << "  offset: " << indexBufferOffset_ << "\n"
        << "    new RVB handle: " << renderVertexBuffer
        << "  offset: " << renderVertexBufferOffset_ << "\n"
        << "    new RIB handle: " << renderIndexBuffer
        << "  offset: " << renderIndexBufferOffset_ << "\n";
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

glm::vec3 Model::getBoundingBoxMin() {
    glm::vec3 minBound, maxBound;
    calculateBoundingBox(vertices, minBound, maxBound);
    return minBound;
}

glm::vec3 Model::getBoundingBoxMax() {
    glm::vec3 minBound, maxBound;
    calculateBoundingBox(vertices, minBound, maxBound);
    return maxBound;
}

void Model::loadModel(const std::string& modelPath) {
    // Reset transform when loading new model
    modelMatrix = glm::mat4(1.0f);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str())) {
        throw std::runtime_error(warn + err);
    }

    vertices.clear();
    indices.clear();
    renderVertices.clear();
    renderIndices.clear();
    hasSplitRenderMesh = false;

    // Create vertices directly from OBJ vertex list
    size_t vertexCount = attrib.vertices.size() / 3;
    vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i].pos = {
            attrib.vertices[3 * i + 0],
            attrib.vertices[3 * i + 1],
            attrib.vertices[3 * i + 2]
        };
        vertices[i].color = { 1.0f, 1.0f, 1.0f };
        // Set default normal 
        vertices[i].normal = { 0.0f, 0.0f, 1.0f };
        vertices[i].texCoord = { 0.0f, 0.0f }; // Default UV
    }

    bool hasAnyCornerNormal = false;
    bool hasMissingCornerNormal = false;
    std::unordered_map<ModelCornerKey, uint32_t, ModelCornerKeyHash> renderVertexMap;
    renderVertexMap.reserve(attrib.vertices.size());

    // Process faces and build topology + render indices
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            if (index.vertex_index < 0 || static_cast<size_t>(index.vertex_index) >= vertices.size()) {
                continue;
            }

            // Use the original vertex index directly
            indices.push_back(index.vertex_index);

            // Update texture coordinates if available
            if (index.texcoord_index >= 0 && index.texcoord_index < attrib.texcoords.size() / 2) {
                vertices[index.vertex_index].texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }

            // Build render mesh keyed by OBJ corner indices (v/vt/vn)
            ModelCornerKey key{};
            key.vertexIndex = index.vertex_index;
            key.texcoordIndex = index.texcoord_index;
            key.normalIndex = index.normal_index;

            auto it = renderVertexMap.find(key);
            if (it == renderVertexMap.end()) {
                Vertex renderVertex{};
                renderVertex.pos = vertices[index.vertex_index].pos;
                renderVertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
                renderVertex.texCoord = vertices[index.vertex_index].texCoord;

                if (index.texcoord_index >= 0 && index.texcoord_index < attrib.texcoords.size() / 2) {
                    renderVertex.texCoord = glm::vec2(
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    );
                }

                renderVertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                if (index.normal_index >= 0 && index.normal_index < attrib.normals.size() / 3) {
                    hasAnyCornerNormal = true;
                    renderVertex.normal = glm::vec3(
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    );

                    const float n2 = glm::dot(renderVertex.normal, renderVertex.normal);
                    if (n2 > 1e-12f) {
                        renderVertex.normal *= (1.0f / std::sqrt(n2));
                    } else {
                        renderVertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    }
                } else {
                    hasMissingCornerNormal = true;
                }

                const uint32_t newRenderIndex = static_cast<uint32_t>(renderVertices.size());
                renderVertices.push_back(renderVertex);
                renderIndices.push_back(newRenderIndex);
                renderVertexMap.emplace(key, newRenderIndex);
            } else {
                renderIndices.push_back(it->second);
            }
        }
    }

    // If file has no authored corner normals, use area-weighted fallback on render mesh.
    if (!hasAnyCornerNormal || hasMissingCornerNormal) {
        recalculateNormals();
    }

    // Fallback to welded data if split path failed to build.
    if (renderVertices.empty() || renderIndices.empty()) {
        renderVertices = vertices;
        renderIndices = indices;
        recalculateNormals();
        hasSplitRenderMesh = false;
    } else {
        hasSplitRenderMesh = true;
    }

    modelPosition = glm::vec3(0.0f);
}

void Model::createVertexBuffer() {
    if (vertices.empty()) {
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferOffset_ = 0;
        return;
    }

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

    auto [vertexBufferHandle, vertexBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
        vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
    );

    commandPool.copyBuffer(stagingBuffer, stagingOffset, vertexBufferHandle, vertexBufferOffset, bufferSize);
    memoryAllocator.free(stagingBuffer, stagingOffset);

    vertexBuffer = vertexBufferHandle;
    vertexBufferOffset_ = vertexBufferOffset; 
}

void Model::createIndexBuffer() {
    if (indices.empty()) {
        indexBuffer = VK_NULL_HANDLE;
        indexBufferOffset_ = 0;
        return;
    }

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    std::cout << "[BUFFER] createIndexBuffer(): uploading "
        << indices.size() << " indices\n";

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, indices.data(), static_cast<size_t>(bufferSize));

    auto [indexBufferHandle, indexBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );

    commandPool.copyBuffer(stagingBuffer, stagingOffset, indexBufferHandle, indexBufferOffset, bufferSize);
    memoryAllocator.free(stagingBuffer, stagingOffset);

    indexBuffer = indexBufferHandle;
    indexBufferOffset_ = indexBufferOffset;
}

void Model::createRenderVertexBuffer() {
    if (renderVertices.empty()) {
        renderVertexBuffer = VK_NULL_HANDLE;
        renderVertexBufferOffset_ = 0;
        return;
    }

    VkDeviceSize bufferSize = sizeof(renderVertices[0]) * renderVertices.size();

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, renderVertices.data(), static_cast<size_t>(bufferSize));

    auto [bufferHandle, bufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
    );

    commandPool.copyBuffer(stagingBuffer, stagingOffset, bufferHandle, bufferOffset, bufferSize);
    memoryAllocator.free(stagingBuffer, stagingOffset);

    renderVertexBuffer = bufferHandle;
    renderVertexBufferOffset_ = bufferOffset;
}

void Model::createRenderIndexBuffer() {
    if (renderIndices.empty()) {
        renderIndexBuffer = VK_NULL_HANDLE;
        renderIndexBufferOffset_ = 0;
        return;
    }

    VkDeviceSize bufferSize = sizeof(renderIndices[0]) * renderIndices.size();

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, renderIndices.data(), static_cast<size_t>(bufferSize));

    auto [bufferHandle, bufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );

    commandPool.copyBuffer(stagingBuffer, stagingOffset, bufferHandle, bufferOffset, bufferSize);
    memoryAllocator.free(stagingBuffer, stagingOffset);

    renderIndexBuffer = bufferHandle;
    renderIndexBufferOffset_ = bufferOffset;
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

    // Adjust vertices toward centroids
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

void Model::recalculateNormals() {
    // Recompute normals for render mesh only.
    // Topology mesh stays untouched for contact/intrinsic systems.
    if (renderVertices.empty() || renderIndices.empty()) {
        return;
    }

    for (auto& vertex : renderVertices) {
        vertex.normal = glm::vec3(0.0f);
    }

    for (size_t i = 0; i + 2 < renderIndices.size(); i += 3) {
        const uint32_t i0 = renderIndices[i];
        const uint32_t i1 = renderIndices[i + 1];
        const uint32_t i2 = renderIndices[i + 2];
        if (i0 >= renderVertices.size() || i1 >= renderVertices.size() || i2 >= renderVertices.size()) {
            continue;
        }

        const glm::vec3 v0 = renderVertices[i0].pos;
        const glm::vec3 v1 = renderVertices[i1].pos;
        const glm::vec3 v2 = renderVertices[i2].pos;

        const glm::vec3 faceNormal = glm::cross(v1 - v0, v2 - v0);
        const float area2 = glm::dot(faceNormal, faceNormal);
        if (area2 < 1e-20f) {
            continue;
        }

        renderVertices[i0].normal += faceNormal;
        renderVertices[i1].normal += faceNormal;
        renderVertices[i2].normal += faceNormal;
    }

    for (auto& vertex : renderVertices) {
        const float len2 = glm::dot(vertex.normal, vertex.normal);
        if (len2 > 1e-12f) {
            vertex.normal *= (1.0f / std::sqrt(len2));
        } else {
            vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
}

void Model::updateGeometry(const std::vector<Vertex>& newVertices, const std::vector<uint32_t>& newIndices) {
    vertices = newVertices;
    indices = newIndices;
    // Topology edits invalidate OBJ corner-split mapping; mirror topology for render.
    renderVertices = vertices;
    renderIndices = indices;
    recalculateNormals();
    hasSplitRenderMesh = false;

    updateVertexBuffer();
    updateIndexBuffer();
    updateRenderVertexBuffer();
    updateRenderIndexBuffer();
}

void Model::translate(const glm::vec3& translation) {    
    modelMatrix[3][0] += translation.x;
    modelMatrix[3][1] += translation.y;
    modelMatrix[3][2] += translation.z;
    
    // Update model position
    modelPosition += translation;
}

void Model::rotate(float angleRadians, const glm::vec3& axis, const glm::vec3& pivot) {    
    glm::mat4 translateToPivot = glm::translate(glm::mat4(1.0f), -pivot);
    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angleRadians, axis);
    glm::mat4 translateBack = glm::translate(glm::mat4(1.0f), pivot);
    
    modelMatrix = translateBack * rotation * translateToPivot * modelMatrix;
    
    // Update model position 
    modelPosition = glm::vec3(modelMatrix[3]);
}

void Model::updateVertexBuffer() {
    if (vertices.empty() || vertexBuffer == VK_NULL_HANDLE) {
        return;
    }

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
    commandPool.copyBuffer(stagingBuffer, stagingOffset, vertexBuffer, vertexBufferOffset_, bufferSize);

    // Free staging buffer
    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void Model::updateIndexBuffer() {
    if (indices.empty() || indexBuffer == VK_NULL_HANDLE) {
        return;
    }

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
    commandPool.copyBuffer(stagingBuffer, stagingOffset, indexBuffer, indexBufferOffset_, bufferSize);

    // Free staging buffer
    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void Model::updateRenderVertexBuffer() {
    if (renderVertices.empty() || renderVertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize bufferSize = sizeof(renderVertices[0]) * renderVertices.size();

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, renderVertices.data(), static_cast<size_t>(bufferSize));

    commandPool.copyBuffer(stagingBuffer, stagingOffset, renderVertexBuffer, renderVertexBufferOffset_, bufferSize);
    memoryAllocator.free(stagingBuffer, stagingOffset);
}

void Model::updateRenderIndexBuffer() {
    if (renderIndices.empty() || renderIndexBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceSize bufferSize = sizeof(renderIndices[0]) * renderIndices.size();

    auto [stagingBuffer, stagingOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator.getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, renderIndices.data(), static_cast<size_t>(bufferSize));

    commandPool.copyBuffer(stagingBuffer, stagingOffset, renderIndexBuffer, renderIndexBufferOffset_, bufferSize);
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

void Model::cleanup() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(vertexBuffer, vertexBufferOffset_);
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferOffset_ = 0;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(indexBuffer, indexBufferOffset_);
        indexBuffer = VK_NULL_HANDLE;
        indexBufferOffset_ = 0;
    }
    if (renderVertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(renderVertexBuffer, renderVertexBufferOffset_);
        renderVertexBuffer = VK_NULL_HANDLE;
        renderVertexBufferOffset_ = 0;
    }
    if (renderIndexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(renderIndexBuffer, renderIndexBufferOffset_);
        renderIndexBuffer = VK_NULL_HANDLE;
        renderIndexBufferOffset_ = 0;
    }
}
