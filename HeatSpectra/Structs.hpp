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
}; // 208 bytes

struct GridUniformBufferObject {
    alignas(16) glm::mat4 view; // 16 byte aligned
    alignas(16) glm::mat4 proj; // 16 byte aligned
    alignas(16) glm::vec3 pos;  // 16 byte aligned
}; // 144 bytes

struct LightUniformBufferObject {
    alignas(16) glm::vec3 lightPos_Key; // 16 byte aligned
    alignas(16) glm::vec3 lightPos_Rim; // 16 byte aligned
    alignas(16) glm::vec3 lightAmbient; // 16 byte aligned
}; // 48 bytes

struct SSAOKernelBufferObject {
    alignas(16) glm::vec4 SSAOKernel[16]; 
};

struct HitResult {
    bool hit;
    float distance;
    uint32_t vertexIndex;
    uint32_t vertexIndices[3];
};

struct TimeUniform {
    alignas(8) float deltaTime;    // 8-byte aligned
    alignas(8) float totalTime;    // 8-byte aligned
};

struct SurfaceVertex {
    alignas(16) glm::vec3 position; // 16 byte aligned
    alignas(16) glm::vec3 color;    // 16 byte aligned
};

struct TetrahedralElement {
    uint32_t vertices[4];  
    float temperature;  
    float coolingRate;        
    float thermalConductivity;
};  
    
struct FEAMesh {
    std::vector<TetrahedralElement> elements;
    std::vector<glm::vec4> nodes; 
    std::vector<glm::vec4> tetraCenters;
    std::vector<float> nodeTemps;     
};

struct TetraFrameBuffers {
    VkBuffer readBuffer;
    VkDeviceMemory readBufferMemory;
    VkBuffer writeBuffer;
    VkDeviceMemory writeBufferMemory;
    void* mappedReadData;
    void* mappedWriteData;
};

struct HeatSourceVertex {
    alignas(16) glm::vec3 position;  // Matches your existing vertex format
    alignas(16) glm::vec3 color;     // Will be set by compute shader
    alignas(4) float temperature;    // Initial temperature value
};

