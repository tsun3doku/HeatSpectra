#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <cstdint>

//
//                                                  [ Proper alignment here is important  
//                                                    - SIMD requires 16 byte alignment 
//                                                    - glm::vec3 is a 12 byte container ]
//
// 

// Main UBO
struct UniformBufferObject {
    alignas(16) glm::mat4 model; // 16 byte aligned 
    alignas(16) glm::mat4 view;  // 16 byte aligned
    alignas(16) glm::mat4 proj;  // 16 byte aligned
    alignas(16) glm::vec3 color; // 16 byte aligned 
};  // 208 bytes

struct GridUniformBufferObject {
    alignas(16) glm::mat4 view; // 16 byte aligned
    alignas(16) glm::mat4 proj; // 16 byte aligned
    alignas(16) glm::vec3 pos;  // 16 byte aligned
};  // 144 bytes

struct LightUniformBufferObject {
    alignas(16) glm::vec3 lightPos_Key; // 16 byte aligned
    alignas(16) glm::vec3 lightPos_Rim; // 16 byte aligned
    alignas(16) glm::vec3 lightAmbient; // 16 byte aligned
};  // 48 bytes

struct SSAOKernelBufferObject {
    alignas(16) glm::vec4 SSAOKernel[16]; // 16 byte aligned
};  // 256 bytes

struct Suballocation {
    VkDeviceSize offset;
    VkDeviceSize size;
    bool isFree;
};

struct AllocatorStats {
    VkDeviceSize totalAllocated = 0;
    VkDeviceSize usedBytes = 0;
    uint32_t allocationCount = 0;
};

struct HitResult {
    bool hit;
    float distance;
    uint32_t vertexIndex;
    uint32_t vertexIndices[3];
    int edgeIndex = -1; 
};

struct TimeUniform {
    float deltaTime;    
    float totalTime;    
};  // 8 bytes

struct SurfaceVertex {
    glm::vec4 position; // 16 byte aligned
    glm::vec4 color;    // 16 byte aligned
};  // 32 bytes

struct TetrahedralElement {
    uint32_t vertices[4];
    float temperature;
    float volume;
    float density;
    float specificHeat;
    float conductivity;
    float coolingRate;
};

struct FEAMesh {
    std::vector<TetrahedralElement> elements;
    std::vector<glm::vec4> nodes;
    std::vector<glm::vec4> tetraCenters;
    std::vector<float> nodeTemps;
    std::vector<std::vector<uint32_t>> neighbors;
};

struct TetraFrameBuffers {
    std::vector<VkBuffer> readBuffers;
    std::vector<VkDeviceMemory> readBufferMemories;
    std::vector<VkDeviceSize> readBufferOffsets_;
    std::vector<VkBuffer> writeBuffers;
    std::vector<VkDeviceMemory> writeBufferMemories;
    std::vector<VkDeviceSize> writeBufferOffsets_;
    std::vector<void*> mappedReadData;
    std::vector<void*> mappedWriteData;
};

struct HeatSourcePushConstant {
    alignas(16) glm::mat4 heatSourceModelMatrix; // Heat source (torus) transform
    alignas(16) glm::mat4 visModelMatrix;         // VisModel (tet mesh) transform
};

struct GeometryPushConstant {
    alignas(16) glm::mat4 modelMatrix; // 16 byte aligned
    int32_t useHeatColors;
};

struct OutlinePushConstant {
    float outlineThickness;
    uint32_t selectedModelID;
    alignas(16) glm::vec3 outlineColor;
};  // 32 bytes

struct HeatSourceVertex {
    glm::vec4 position; // 16 byte aligned
    float temperature;
    float padding[3];
};  // 32 bytes

struct FaceRef {
    uint32_t faceIndex;
    uint8_t edgeNum;
};

struct Edge {
    uint32_t first, second;
    Edge(uint32_t a, uint32_t b) : first(std::min(a, b)), second(std::max(a, b)) {}
    bool operator==(const Edge& other) const {
        return first == other.first && second == other.second;
    }
};

struct EdgeData {
    uint32_t v1, v2;
    float length;
    bool isFeature = false;
    bool isConstraint = false;
    std::vector<uint32_t> adjacentFaces;
    float targetLength = 0.0f;
};

struct EdgeHash {
    size_t operator()(const Edge& e) const {
        size_t seed = 0;
        hash_combine(seed, e.first);
        hash_combine(seed, e.second);
        return seed;
    }

private:
    template <class T>
    inline void hash_combine(size_t& seed, const T& v) const {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};

struct Vec3Hash {
    size_t operator()(const glm::vec3& v) const {
        return std::hash<float>{}(v.x) ^
            (std::hash<float>{}(v.y) << 1) ^
            (std::hash<float>{}(v.z) << 2);
    }
};

// Axis-Aligned Bounding Box
struct AABB {
    glm::vec3 min{ FLT_MAX };
    glm::vec3 max{ -FLT_MAX };

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    bool contains(const glm::vec3& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
            (point.y >= min.y && point.y <= max.y) &&
            (point.z >= min.z && point.z <= max.z);
    }

    bool intersects(const AABB& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
            (min.y <= other.max.y && max.y >= other.min.y) &&
            (min.z <= other.max.z && max.z >= other.min.z);
    }

    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }
};

// Node for AABB Tree
struct AABBNode {
    AABB bounds;
    std::vector<uint32_t> triangleIndices;
    std::unique_ptr<AABBNode> left;
    std::unique_ptr<AABBNode> right;
    bool isLeaf = false;
};
