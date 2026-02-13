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

struct SurfacePoint {
    glm::vec3 position;        // 12 bytes
    float temperature;         // 4 bytes (offset 12)
    glm::vec3 normal;          // 12 bytes (offset 16)
    float area;                // 4 bytes (offset 28)
    glm::vec4 color;           // 16 bytes (offset 32)
};  // 48 bytes

struct VoronoiSurfaceMapping {
    uint32_t cellIndex;            // Nearest Voronoi cell index
    uint32_t _padding[3];          // GPU alignment to 16 bytes
};  // 16 bytes

struct IntrinsicTriangleData {
    glm::vec3 center;              // Triangle centroid
    float area;                    // Triangle area
    glm::vec3 normal;              // Face normal 
    float padding;                
};  // 32 bytes

struct HeatSourceTriangleGPU {
    glm::vec4 centerArea;
    glm::vec4 normalPad;
    glm::uvec4 indices;
};

struct ContactSampleGPU {
    uint32_t sourceTriangleIndex;
    float u;
    float v;
    float wArea;
};

struct ContactPairGPU {
    ContactSampleGPU samples[7];
    float contactArea;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct IntrinsicVertexData {
    glm::vec3 position;            // 3D world position
    uint32_t intrinsicVertexId;    // ID in intrinsic mesh
    glm::vec3 normal;              // Area weighted vertex normal
    float padding;                 
};   

struct SurfelParams {              // Not used may remove soon
    float thermalConductance;  
    float contactPressure;     
    float frictionCoeff;      
    float padding;         
};

struct HeatSourcePushConstant {
    alignas(16) glm::mat4 heatSourceModelMatrix;    
    alignas(16) glm::mat4 visModelMatrix;
    alignas(16) glm::mat4 inverseHeatSourceModelMatrix;
    uint32_t maxNodeNeighbors;
    uint32_t substepIndex;      // 0 = update display
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

struct VoronoiNeighborGPU {
    uint32_t neighborIndex;     
    float interfaceArea;        // Area of the voronoi face shared with this neighbor
    float distance;             // Distance to neighbor
    uint32_t interfaceFaceID;  
};

struct VoronoiNodeGPU {
    float temperature;
    float prevTemperature;
    float thermalMass;      
    float density;
    float specificHeat;
    float conductivity;
    
    float volume;               
    uint32_t neighborOffset;    
    uint32_t neighborCount;     
    uint32_t interfaceNeighborCount; 
};

struct DebugCellGeometry {
    uint32_t cellID;
    uint32_t vertexCount;
    uint32_t triangleCount;
    float volume;
    glm::vec4 vertices[48];
    glm::uvec4 triangles[96]; 
};

const uint32_t DEBUG_MAX_TRIANGLES = 64;

struct VolumeChunkInfo {
    uint32_t triIndex;          // Triangle index
    uint32_t skipped;           // 0=processed, 1=skip@plane0, 2=plane1, 3=plane2, 4=cap 
    uint32_t goOut;             // 1 if go_out_triangle, 0 otherwise 
    float triDet;               // Shadow cone determinant 
    float chunkVolume;          // Volume contribution 
    float chunkDet;             // Volume determinant for this chunk 
    float cumulativeVolume;     // Running total after this chunk 
    float padding;             
};  

struct VoronoiDumpInfo {
    uint32_t cellID;
    uint32_t isGhost;
    uint32_t _padding0;
    uint32_t originOccupancy;    // Domain status of origin (0=outside, 1=border, 2=inside)
    
    glm::vec4 seedPos;           
    
    // Voxel corner range 
    glm::ivec4 cornerMin;
    glm::ivec4 cornerMax;
    
    uint32_t inDomain;           // 0=outside, 1=border, 2=inside
    uint32_t addVolume;          // 1 if volume added initially
    uint32_t cornerCount;        // Corners checked
    uint32_t _padding;           // Alignment to 16 bytes

    glm::ivec4 firstInsideCorner; 
    glm::ivec4 firstOutsideCorner; 
    
    // Origin of tetra
    glm::vec4 origin;             
    
    float unrestrictedVolume;
    float restrictedVolume;
    float maxChunkDet;          
    float totalRestrictedVolume;
    
    // Volume chunks breakdown (32 * 64 = 2048 bytes)
    VolumeChunkInfo volumeChunks[DEBUG_MAX_TRIANGLES];
    
    // Voxel corner occupancy (4096 bytes)
    uint32_t cornerOccupancy[1024];
};  

// Number of cells to capture debug info for
const uint32_t DEBUG_DUMP_CELL_COUNT = 8;
