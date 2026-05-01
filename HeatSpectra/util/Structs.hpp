#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <cstdint>

//
//                                                  [ Proper alignment here is important  
//                                                    - SIMD requires 16 byte vector alignment 
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
    alignas(16) glm::mat4 view;         // 16 byte aligned
    alignas(16) glm::mat4 proj;         // 16 byte aligned
    alignas(16) glm::vec3 pos;          // 16 byte aligned
    alignas(16) glm::vec3 gridSize;     // 16 byte aligned 
};  // 128 bytes

struct LightUniformBufferObject {
    alignas(16) glm::vec3 lightPos_Key; // 16 byte aligned
    alignas(16) glm::vec3 lightPos_Rim; // 16 byte aligned
    alignas(16) glm::vec3 lightAmbient; // 16 byte aligned
    alignas(16) glm::vec4 lightParams;  // x=keyIntensity, y=rimIntensity, z=ambientIntensity, w=pad
    alignas(16) glm::vec3 cameraPos;    // Camera world position for specular view direction
    float _padding0 = 0.0f;
};  // 80 bytes

struct MaterialData {
    alignas(16) glm::vec3 baseColor;    
    float roughness;                   
    float specularF0;                   
    float _padding1 = 0.0f;
    float _padding2 = 0.0f;
};  // 32 bytes

struct MaterialUniformBufferObject {
    alignas(16) glm::vec4 baseColorRoughness; // xyz=baseColor, w=roughness
    alignas(16) glm::vec4 specular;           // x=specularF0, yzw=reserved
};  // 32 bytes

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

struct IntrinsicTriangleData {
    glm::vec3 center;              // Triangle centroid
    float area;                    // Triangle area
    glm::vec3 normal;              // Face normal 
    float padding;                
};  // 32 bytes

struct IntrinsicVertexData {
    glm::vec3 position;            // 3D world position
    uint32_t intrinsicVertexId;    // ID in intrinsic mesh
    glm::vec3 normal;              // Area weighted vertex normal
    float padding;                 
};   

struct GeometryPushConstant {
    alignas(16) glm::mat4 modelMatrix; 
    float alpha;            
    int32_t _padding[3];    
};

struct OutlinePushConstant {
    float outlineThickness;
    uint32_t selectedModelID;
    alignas(16) glm::vec3 outlineColor;
};  

struct NormalPushConstant {
    alignas(16) glm::mat4 modelMatrix;
    float normalLength;
    float avgArea;
};