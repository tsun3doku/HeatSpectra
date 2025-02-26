#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    alignas(16) glm::mat4 model; // 16 byte aligned
};

struct HeatSourceVertex {
    glm::vec4 position; // 16 byte aligned
    float temperature;
    float padding[3];
};  // 32 bytes
