#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"

#include "Camera.hpp"
#include "Model.hpp"
#include "util/Structs.hpp"

bool Model::init() {
    if (vertices.empty() || indices.empty() ||
        renderVertices.empty() || renderIndices.empty()) {
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

bool Model::getLocalBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    if (vertices.empty()) return false;

    outMin = vertices.front().pos;
    outMax = vertices.front().pos;
    for (const Vertex& vertex : vertices) {
        outMin = glm::min(outMin, vertex.pos);
        outMax = glm::max(outMax, vertex.pos);
    }
    return true;
}

bool Model::getWorldBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    glm::vec3 localMin(0.0f);
    glm::vec3 localMax(0.0f);
    if (!getLocalBounds(localMin, localMax)) return false;

    const std::array<glm::vec3, 8> corners{
        glm::vec3(localMin.x, localMin.y, localMin.z),
        glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z),
        glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z),
        glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z),
        glm::vec3(localMax.x, localMax.y, localMax.z)};

    const glm::vec3 firstCorner = glm::vec3(modelMatrix * glm::vec4(corners.front(), 1.0f));
    outMin = firstCorner;
    outMax = firstCorner;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const glm::vec3 worldCorner = glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f));
        outMin = glm::min(outMin, worldCorner);
        outMax = glm::max(outMax, worldCorner);
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

std::vector<Vertex> Model::makeVertices(
    const std::vector<float>& positions,
    const std::vector<float>* normals,
    const std::vector<float>* texcoords) {
    const std::size_t vertexCount = positions.size() / 3;
    std::vector<Vertex> result(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        Vertex& vertex = result[i];
        vertex.pos = glm::vec3(
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]);
        vertex.color = glm::vec3(1.0f);
        vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        vertex.texCoord = glm::vec2(0.0f);
        if (normals && normals->size() == vertexCount * 3) {
            vertex.normal = glm::vec3(
                (*normals)[i * 3 + 0],
                (*normals)[i * 3 + 1],
                (*normals)[i * 3 + 2]);
        }
        if (texcoords && texcoords->size() == vertexCount * 2) {
            vertex.texCoord = glm::vec2(
                (*texcoords)[i * 2 + 0],
                (*texcoords)[i * 2 + 1]);
        }
    }
    return result;
}

void Model::setGeometry(
    const std::vector<float>& positions,
    const std::vector<uint32_t>& newIndices) {
    vertices = makeVertices(positions);
    indices = newIndices;
    renderVertices = vertices;
    renderIndices = indices;
    hasSplitRenderMesh = false;
    recalculateNormals();
}

void Model::setRenderGeometry(
    const std::vector<float>& positions,
    const std::vector<float>& normals,
    const std::vector<float>& texcoords,
    const std::vector<uint32_t>& newIndices) {
    renderVertices = makeVertices(positions, &normals, &texcoords);
    renderIndices = newIndices;
    hasSplitRenderMesh = renderVertices != vertices || renderIndices != indices;

    bool missingNormal = normals.size() != renderVertices.size() * 3;
    if (!missingNormal) {
        for (const Vertex& vertex : renderVertices) {
            if (glm::dot(vertex.normal, vertex.normal) <= 1e-12f) {
                missingNormal = true;
                break;
            }
        }
    }
    if (missingNormal) recalculateNormals();
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
