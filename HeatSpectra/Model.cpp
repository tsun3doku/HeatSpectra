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

void Model::buildEdgeFaceMap() {
    edgeFaceMap.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) continue; 

        for (int j = 0; j < 3; j++) {
            uint32_t v0 = indices[i + j];
            uint32_t v1 = indices[i + (j + 1) % 3];
            if (v0 >= vertices.size() || v1 >= vertices.size()) continue; 

            Edge e(v0, v1);
            FaceRef faceRef;
            faceRef.baseIndex = i;
            faceRef.edgeNum = j;
            edgeFaceMap[e].push_back(faceRef);
        }
    }
}

void Model::buildVertexAdjacency() {
    vertexAdjacency.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t v0 = indices[i];
        uint32_t v1 = indices[i + 1];
        uint32_t v2 = indices[i + 2];

        vertexAdjacency[v0].insert(v1);
        vertexAdjacency[v0].insert(v2);
        vertexAdjacency[v1].insert(v0);
        vertexAdjacency[v1].insert(v2);
        vertexAdjacency[v2].insert(v0);
        vertexAdjacency[v2].insert(v1);
    }
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

    // Edge angle threshold for sharp edges (in radians)
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

    // Process each vertex to determine if it's on a sharp edge
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
            // For vertices on sharp edges, group similar-facing normals
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

void Model::laplacianSmooth(float factor) {
    buildVertexAdjacency();
    std::vector<glm::vec3> newPositions(vertices.size(), glm::vec3(0));
    std::vector<int> neighborCounts(vertices.size(), 0);

    for (const auto& [vIdx, neighbors] : vertexAdjacency) {
        for (uint32_t n : neighbors) {
            newPositions[vIdx] += vertices[n].pos;
            neighborCounts[vIdx]++;
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        if (neighborCounts[i] > 0) {
            glm::vec3 avg = newPositions[i] / float(neighborCounts[i]);
            vertices[i].pos = glm::mix(vertices[i].pos, avg, factor);
        }
    }
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
    recreateBuffers();
}

void Model::voronoiTessellate(int iterations) {
    for (int currentIter = 0; currentIter < iterations; currentIter++) {
        std::vector<Vertex> newVertices = vertices;
        std::vector<uint32_t> newIndices;
        std::unordered_map<Edge, uint32_t, EdgeHash> edgeMidpoints;

        // First pass: Create edge midpoints
        for (size_t i = 0; i < indices.size(); i += 3) {
            const uint32_t v0 = indices[i];
            const uint32_t v1 = indices[i + 1];
            const uint32_t v2 = indices[i + 2];

            auto createMidpoint = [&](uint32_t a, uint32_t b) {
                Edge edge(std::min(a, b), std::max(a, b));
                if (edgeMidpoints.count(edge)) return edgeMidpoints[edge];

                Vertex mid;
                mid.pos = (vertices[a].pos + vertices[b].pos) * 0.5f;
                mid.normal = glm::normalize(vertices[a].normal + vertices[b].normal);
                mid.texCoord = (vertices[a].texCoord + vertices[b].texCoord) * 0.5f;
                mid.color = (vertices[a].color + vertices[b].color) * 0.5f;

                const uint32_t midIdx = static_cast<uint32_t>(newVertices.size());
                newVertices.push_back(mid);
                edgeMidpoints[edge] = midIdx;
                return midIdx;
                };

            createMidpoint(v0, v1);
            createMidpoint(v1, v2);
            createMidpoint(v2, v0);
        }

        // Second pass: Create centroids and triangles
        for (size_t i = 0; i < indices.size(); i += 3) {
            const uint32_t v0 = indices[i];
            const uint32_t v1 = indices[i + 1];
            const uint32_t v2 = indices[i + 2];

            // Create centroid
            Vertex centroid;
            centroid.pos = (vertices[v0].pos + vertices[v1].pos + vertices[v2].pos) / 3.0f;
            centroid.normal = glm::normalize(vertices[v0].normal + vertices[v1].normal + vertices[v2].normal);
            centroid.texCoord = (vertices[v0].texCoord + vertices[v1].texCoord + vertices[v2].texCoord) / 3.0f;
            centroid.color = (vertices[v0].color + vertices[v1].color + vertices[v2].color) / 3.0f;
            const uint32_t centroidIdx = static_cast<uint32_t>(newVertices.size());
            newVertices.push_back(centroid);

            // Get midpoints
            Edge e0(std::min(v0, v1), std::max(v0, v1));
            Edge e1(std::min(v1, v2), std::max(v1, v2));
            Edge e2(std::min(v2, v0), std::max(v2, v0));
            const uint32_t m0 = edgeMidpoints[e0];
            const uint32_t m1 = edgeMidpoints[e1];
            const uint32_t m2 = edgeMidpoints[e2];

            // Add 6 new triangles per original triangle
            newIndices.insert(newIndices.end(), { v0, m0, centroidIdx });
            newIndices.insert(newIndices.end(), { m0, v1, centroidIdx });
            newIndices.insert(newIndices.end(), { v1, m1, centroidIdx });
            newIndices.insert(newIndices.end(), { m1, v2, centroidIdx });
            newIndices.insert(newIndices.end(), { v2, m2, centroidIdx });
            newIndices.insert(newIndices.end(), { m2, v0, centroidIdx });
        }

        vertices = newVertices;
        indices = newIndices;
        buildEdgeFaceMap();
    }
    recreateBuffers();
}

void Model::midpointSubdivide(int iterations, bool preserveShape) {
    for (int iter = 0; iter < iterations; iter++) {
        std::unordered_map<Edge, uint32_t, EdgeHash> edgeMap;
        std::vector<Vertex> newVertices = vertices;
        std::vector<uint32_t> newIndices;

        // Lambda to get/create edge midpoints
        auto getMidpoint = [&](uint32_t a, uint32_t b) {
            Edge edge(std::min(a, b), std::max(a, b));
            if (edgeMap.count(edge)) return edgeMap[edge];

            Vertex mid;
            if (preserveShape) {
                // Pure midpoint without smoothing
                mid.pos = (vertices[a].pos + vertices[b].pos) * 0.5f;
                mid.normal = glm::normalize(vertices[a].normal + vertices[b].normal);
                mid.texCoord = (vertices[a].texCoord + vertices[b].texCoord) * 0.5f;
            }
            else {
                // Optional: Add smoothing for non-FEA use
                mid.pos = vertices[a].pos * 0.5f + vertices[b].pos * 0.5f;
                mid.normal = glm::normalize(vertices[a].normal + vertices[b].normal);
                mid.texCoord = (vertices[a].texCoord + vertices[b].texCoord) * 0.5f;
            }
            mid.color = (vertices[a].color + vertices[b].color) * 0.5f;

            edgeMap[edge] = static_cast<uint32_t>(newVertices.size());
            newVertices.push_back(mid);
            return edgeMap[edge];
            };

        // Process each triangle
        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t v0 = indices[i];
            uint32_t v1 = indices[i + 1];
            uint32_t v2 = indices[i + 2];

            uint32_t m0 = getMidpoint(v0, v1);
            uint32_t m1 = getMidpoint(v1, v2);
            uint32_t m2 = getMidpoint(v2, v0);

            // Split into 4 sub-triangles
            newIndices.insert(newIndices.end(), { v0, m0, m2 });
            newIndices.insert(newIndices.end(), { m0, v1, m1 });
            newIndices.insert(newIndices.end(), { m2, m0, m1 });
            newIndices.insert(newIndices.end(), { m2, m1, v2 });
        }

        vertices = newVertices;
        indices = newIndices;
        buildEdgeFaceMap();
    }

    if (preserveShape) equalizeFaceAreas();
}

void Model::uniformSubdivide(int iterations, float smoothingFactor) {
    for (int iter = 0; iter < iterations; iter++) {
        midpointSubdivide(1, false);
        weldVertices(0.0001f); 
        laplacianSmooth(smoothingFactor);
        equalizeFaceAreas();
    }

    weldVertices(0.0001f); 
    for (int i = 0; i < 2; i++) {
        equalizeFaceAreas();
        laplacianSmooth(0.1f);
    }
    recreateBuffers();
}

void Model::isotropicRemesh(float targetEdgeLength, int iterations) {
    // Store original vertices for attribute preservation
    std::vector<Vertex> originalVertices = vertices;

    // Convert to remesher format
    std::vector<Vector3> remeshVertices;
    std::vector<std::vector<size_t>> remeshTriangles;

    for (const auto& vertex : vertices) {
        remeshVertices.emplace_back(vertex.pos.x, vertex.pos.y, vertex.pos.z);
    }

    for (size_t i = 0; i < indices.size(); i += 3) {
        remeshTriangles.push_back({
            static_cast<size_t>(indices[i]),
            static_cast<size_t>(indices[i + 1]),
            static_cast<size_t>(indices[i + 2])
            });
    }

    // Create and configure remesher
    IsotropicRemesher remesher(&remeshVertices, &remeshTriangles);
    remesher.setTargetEdgeLength(targetEdgeLength);

    // Use a lower angle threshold to better detect sharp edges
    // Adjust this value to control which edges are considered "sharp"
    remesher.setSharpEdgeIncludedAngle(30.0);

    // Pre-detection of sharp features
    auto* halfedgeMesh = remesher.remeshedHalfedgeMesh();
    halfedgeMesh->updateTriangleNormals();
    halfedgeMesh->featureEdges(25.0 * (M_PI / 180.0)); // Convert to radians
    halfedgeMesh->featureBoundaries(); // Mark boundaries as features

    remesher.remesh(iterations);

    IsotropicHalfedgeMesh* result = remesher.remeshedHalfedgeMesh();
    std::vector<Vertex> newVertices;
    std::vector<uint32_t> newIndices;
    std::unordered_map<IsotropicHalfedgeMesh::Vertex*, uint32_t> vertexMap;

    // Process all faces
    for (auto* face = result->moveToNextFace(nullptr);
        face != nullptr;
        face = result->moveToNextFace(face)) {

        auto* he = face->halfedge;
        for (int i = 0; i < 3; i++) {
            auto* hv = he->startVertex;

            // Find or create new vertex
            if (!vertexMap.count(hv)) {
                Vertex newVertex;
                newVertex.pos = { hv->position.x(), hv->position.y(), hv->position.z() };

                // Preserve original attributes if possible
                if (hv->originalIndex < originalVertices.size()) {
                    newVertex.normal = originalVertices[hv->originalIndex].normal;
                    newVertex.texCoord = originalVertices[hv->originalIndex].texCoord;
                    newVertex.color = originalVertices[hv->originalIndex].color;
                }
                else {
                    // For new vertices, interpolate attributes from neighboring vertices
                    newVertex.normal = glm::vec3(0.0f);
                    newVertex.texCoord = glm::vec2(0.0f);
                    newVertex.color = glm::vec3(0.8f, 0.8f, 0.8f); // Default color
                }

                if (hv->featured) {
                    newVertex.color = glm::vec3(1.0f, 0.0f, 0.0f); // Highlight feature edges
                }

                vertexMap[hv] = static_cast<uint32_t>(newVertices.size());
                newVertices.push_back(newVertex);
            }

            newIndices.push_back(vertexMap[hv]);
            he = he->nextHalfedge;
        }
    }

    // Update model data
    vertices = newVertices;
    indices = newIndices;

    // Recalculate normals while preserving sharp edges
    recalculateNormals();

    // Optional: less aggressive welding to preserve sharpness
    weldVertices(0.000001f);
    laplacianSmooth(.25);
    recreateBuffers();
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