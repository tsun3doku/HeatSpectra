#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"

void Model::init(VulkanDevice& vulkanDevice, MemoryAllocator& allocator, const std::string modelPath) {
    this->vulkanDevice = &vulkanDevice; 
    this->memoryAllocator = &allocator;

    loadModel(modelPath);
    subdivide();

    createVertexBuffer();
    createIndexBuffer();

    createSurfaceBuffer();
}

Model::Model(VulkanDevice& device, MemoryAllocator& allocator)
    : vulkanDevice(&device), memoryAllocator(&allocator) {
}

Model::~Model() {
    cleanup();
}

void Model::recreateBuffers() {
    cleanup();
    createVertexBuffer();
    createIndexBuffer();

    createSurfaceBuffer();
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

void Model::loadModel(const std::string& modelPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str())) {
        throw std::runtime_error(warn + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // Load position data
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // Load texture coordinates
            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            // Load normals
            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }
            else {

                vertex.normal = { 0.0f, 0.0f, 1.0f };
            }

            vertex.color = { 1.0f, 1.0f, 1.0f };

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }

    modelPosition = getBoundingBoxCenter();
}

void Model::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    auto [stagingBuffer, stagingOffset] = memoryAllocator->allocate(
        bufferSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator->getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, vertices.data(), static_cast<size_t>(bufferSize));

    // Allocate the local device vertex buffer
    auto [vertexBufferHandle, vertexBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
        vulkanDevice->getPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
    );

    // Copy from the staging buffer to the vertex buffer
    copyBuffer(*vulkanDevice, stagingBuffer, stagingOffset, vertexBufferHandle, vertexBufferOffset, bufferSize);

    // Free the staging buffer 
    memoryAllocator->free(stagingBuffer, stagingOffset);

    // Assign handles and offsets
    vertexBuffer = vertexBufferHandle;
    vertexBufferOffset_ = vertexBufferOffset; 
}

void Model::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // Allocate staging buffer using MemoryAllocator
    auto [stagingBuffer, stagingOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    void* stagingData = memoryAllocator->getMappedPointer(stagingBuffer, stagingOffset);
    memcpy(stagingData, indices.data(), static_cast<size_t>(bufferSize));

    // Allocate device-local index buffer using MemoryAllocator
    auto [indexBufferHandle, indexBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vulkanDevice->getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment
    );

    // Copy data with offsets
    copyBuffer(*vulkanDevice, stagingBuffer, stagingOffset, indexBufferHandle, indexBufferOffset, bufferSize);

    // Free staging buffer
    memoryAllocator->free(stagingBuffer, stagingOffset);

    // Assign handles and offsets
    indexBuffer = indexBufferHandle;
    indexBufferOffset_ = indexBufferOffset;
}

void Model::createSurfaceBuffer() {
    VkDeviceSize bufferSize = sizeof(SurfaceVertex) * vertices.size();

    // Allocate surfaceBuffer
    auto [surfaceBufferHandle, surfaceBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    surfaceBuffer = surfaceBufferHandle;
    surfaceBufferOffset_ = surfaceBufferOffset;

    // Allocate surfaceVertexBuffer 
    auto [surfaceVertexBufferHandle, surfaceVertexBufferOffset] = memoryAllocator->allocate(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    surfaceVertexBuffer = surfaceVertexBufferHandle;
    surfaceVertexBufferOffset_ = surfaceVertexBufferOffset;
}

void Model::setSubdivisionLevel(int level) {
    subdivisionLevel = level;
}

void Model::subdivide() {
    for (int i = 0; i < subdivisionLevel; i++) {
        std::vector<Vertex> newVertices = vertices;
        std::vector<uint32_t> newIndices;
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgeMap;
        const float MERGE_EPSILON = 1e-6f; // Merge nearby vertices

        auto safeGetMidpoint = [&](uint32_t a, uint32_t b) {
            // Prevent zero-length edges
            if (glm::distance(vertices[a].pos, vertices[b].pos) < MERGE_EPSILON) {
                return a;
            }

            // Create new midpoint vertex
            Vertex mid;
            mid.pos = (vertices[a].pos + vertices[b].pos) * 0.5f;
            mid.normal = glm::normalize(vertices[a].normal + vertices[b].normal);
            mid.color = (vertices[a].color + vertices[b].color) * 0.5f;
            mid.texCoord = (vertices[a].texCoord + vertices[b].texCoord) * 0.5f;

            // Check if midpoint already exists
            auto it = edgeMap.find({ a, b });
            if (it != edgeMap.end()) {
                return it->second;
            }

            // Store new vertex
            uint32_t index = newVertices.size();
            newVertices.push_back(mid);
            edgeMap[{a, b}] = index;
            edgeMap[{b, a}] = index; // Bidirectional
            return index;
            };

        // Modified subdivision logic
        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t v0 = indices[i];
            uint32_t v1 = indices[i + 1];
            uint32_t v2 = indices[i + 2];

            // Skip degenerate source triangles
            if (v0 == v1 || v1 == v2 || v0 == v2) continue;

            // Create new vertices with safety checks
            uint32_t a = safeGetMidpoint(v0, v1);
            uint32_t b = safeGetMidpoint(v1, v2);
            uint32_t c = safeGetMidpoint(v2, v0);
            //std::cout << "Subdividing..." << std::endl;
           
            // Only add valid new triangles
            if (a != b && b != c && c != a) {
                newIndices.insert(newIndices.end(), { v0, a, c });
                newIndices.insert(newIndices.end(), { v1, b, a });
                newIndices.insert(newIndices.end(), { v2, c, b });
                newIndices.insert(newIndices.end(), { a, b, c });
            }
        }

        vertices = newVertices;
        indices = newIndices;
    }
}

void Model::cleanup() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator->free(vertexBuffer, vertexBufferOffset_);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        memoryAllocator->free(indexBuffer, indexBufferOffset_);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (surfaceBuffer != VK_NULL_HANDLE) {
        memoryAllocator->free(surfaceBuffer, surfaceBufferOffset_);
        surfaceBuffer = VK_NULL_HANDLE;
    }
    if (surfaceVertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator->free(surfaceVertexBuffer, surfaceVertexBufferOffset_);
        surfaceVertexBuffer = VK_NULL_HANDLE;
    }
}