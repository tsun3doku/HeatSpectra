#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <array>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <iostream>

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"

#include "Camera.hpp"
#include "Model.hpp"
#include "MeshImporter.hpp"
#include "util/Structs.hpp"

bool Model::init(const std::string modelPath) {
    if (!loadModel(modelPath)) {
        return false;
    }

    createVertexBuffer();
    createIndexBuffer();
    createRenderVertexBuffer();
    createRenderIndexBuffer();
    return true;
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

bool Model::loadModel(const std::string& modelPath) {
    // Reset transform when loading new model
    modelMatrix = glm::mat4(1.0f);

    vertices.clear();
    indices.clear();
    renderVertices.clear();
    renderIndices.clear();
    hasSplitRenderMesh = false;

    MeshImporter::Mesh importedMesh;
    if (!MeshImporter::loadMesh(modelPath, importedMesh) || !importedMesh.isValid()) {
        std::cerr << "[Model] Failed to load model: " << modelPath << std::endl;
        return false;
    }

    // Build vertices directly from imported vertex position list
    const size_t vertexCount = importedMesh.positions.size() / 3;
    vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i].pos = {
            importedMesh.positions[3 * i + 0],
            importedMesh.positions[3 * i + 1],
            importedMesh.positions[3 * i + 2]
        };
        vertices[i].color = { 1.0f, 1.0f, 1.0f };
        vertices[i].normal = { 0.0f, 0.0f, 1.0f };
        vertices[i].texCoord = { 0.0f, 0.0f };
    }

    bool hasAnyCornerNormal = false;
    bool hasMissingCornerNormal = false;
    std::unordered_map<ModelCornerKey, uint32_t, ModelCornerKeyHash> renderVertexMap;
    renderVertexMap.reserve(importedMesh.corners.size());

    // Process faces and build topology + render indices from imported corners
    for (uint32_t cornerIdx : importedMesh.triangleCornerIndices) {
        const auto& corner = importedMesh.corners[cornerIdx];
        if (corner.vertexIndex < 0 || static_cast<size_t>(corner.vertexIndex) >= vertices.size()) {
            continue;
        }

        indices.push_back(corner.vertexIndex);

        if (corner.texcoordIndex >= 0 && static_cast<size_t>(corner.texcoordIndex * 2 + 1) < importedMesh.texcoords.size()) {
            vertices[corner.vertexIndex].texCoord = {
                importedMesh.texcoords[2 * corner.texcoordIndex + 0],
                1.0f - importedMesh.texcoords[2 * corner.texcoordIndex + 1]
            };
        }

        ModelCornerKey key{};
        key.vertexIndex = corner.vertexIndex;
        key.texcoordIndex = corner.texcoordIndex;
        key.normalIndex = corner.normalIndex;

        auto it = renderVertexMap.find(key);
        if (it == renderVertexMap.end()) {
            Vertex renderVertex{};
            renderVertex.pos = vertices[corner.vertexIndex].pos;
            renderVertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
            renderVertex.texCoord = vertices[corner.vertexIndex].texCoord;

            if (corner.texcoordIndex >= 0 && static_cast<size_t>(corner.texcoordIndex * 2 + 1) < importedMesh.texcoords.size()) {
                renderVertex.texCoord = glm::vec2(
                    importedMesh.texcoords[2 * corner.texcoordIndex + 0],
                    1.0f - importedMesh.texcoords[2 * corner.texcoordIndex + 1]
                );
            }

            renderVertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            if (corner.normalIndex >= 0 && static_cast<size_t>(corner.normalIndex * 3 + 2) < importedMesh.normals.size()) {
                hasAnyCornerNormal = true;
                renderVertex.normal = glm::vec3(
                    importedMesh.normals[3 * corner.normalIndex + 0],
                    importedMesh.normals[3 * corner.normalIndex + 1],
                    importedMesh.normals[3 * corner.normalIndex + 2]
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

    if (!hasAnyCornerNormal || hasMissingCornerNormal) {
        recalculateNormals();
    }

    if (renderVertices.empty() || renderIndices.empty()) {
        renderVertices = vertices;
        renderIndices = indices;
        recalculateNormals();
        hasSplitRenderMesh = false;
    } else {
        hasSplitRenderMesh = true;
    }

    return true;
}

void Model::createVertexBuffer() {
    if (vertices.empty()) {
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferOffset_ = 0;
        return;
    }

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        vertices.data(),
        sizeof(Vertex) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        alignment,
        vertexBuffer,
        vertexBufferOffset_
    );
}

void Model::createIndexBuffer() {
    if (indices.empty()) {
        indexBuffer = VK_NULL_HANDLE;
        indexBufferOffset_ = 0;
        return;
    }

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        indices.data(),
        sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        alignment,
        indexBuffer,
        indexBufferOffset_
    );
}

void Model::createRenderVertexBuffer() {
    if (renderVertices.empty()) {
        renderVertexBuffer = VK_NULL_HANDLE;
        renderVertexBufferOffset_ = 0;
        return;
    }

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        renderVertices.data(),
        sizeof(Vertex) * renderVertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        alignment,
        renderVertexBuffer,
        renderVertexBufferOffset_
    );
}

void Model::createRenderIndexBuffer() {
    if (renderIndices.empty()) {
        renderIndexBuffer = VK_NULL_HANDLE;
        renderIndexBufferOffset_ = 0;
        return;
    }

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        renderIndices.data(),
        sizeof(uint32_t) * renderIndices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        alignment,
        renderIndexBuffer,
        renderIndexBufferOffset_
    );
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

void Model::recalculateNormals() {
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
    if (vertices.empty()) return;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        vertices.data(),
        sizeof(vertices[0]) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        alignment,
        vertexBuffer,
        vertexBufferOffset_
    );
}

void Model::updateIndexBuffer() {
    if (indices.empty()) return;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        indices.data(),
        sizeof(indices[0]) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        alignment,
        indexBuffer,
        indexBufferOffset_
    );
}

void Model::updateRenderVertexBuffer() {
    if (renderVertices.empty()) return;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        renderVertices.data(),
        sizeof(renderVertices[0]) * renderVertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        alignment,
        renderVertexBuffer,
        renderVertexBufferOffset_
    );
}

void Model::updateRenderIndexBuffer() {
    if (renderIndices.empty()) return;
    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    uploadDeviceBuffer(
        memoryAllocator,
        commandPool,
        renderIndices.data(),
        sizeof(renderIndices[0]) * renderIndices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        alignment,
        renderIndexBuffer,
        renderIndexBufferOffset_
    );
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
    freeBuffer(memoryAllocator, vertexBuffer, vertexBufferOffset_);
    freeBuffer(memoryAllocator, indexBuffer, indexBufferOffset_);
    freeBuffer(memoryAllocator, renderVertexBuffer, renderVertexBufferOffset_);
    freeBuffer(memoryAllocator, renderIndexBuffer, renderIndexBufferOffset_);
}
