#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Main UBO
struct UniformBufferObject {
    alignas(16) glm::mat4 model; // 64 bytes 
    alignas(16) glm::mat4 view;  // 64 bytes
    alignas(16) glm::mat4 proj;  // 64 bytes
    alignas(16) glm::vec3 color; // 16 bytes  
}; // 208 bytes

// Grid UBO
struct GridUniformBufferObject {
    alignas(16) glm::mat4 view; // 64 bytes
    alignas(16) glm::mat4 proj; // 64 bytes
    alignas(16) glm::vec3 pos;  // 16 bytes
}; // 144 bytes

// Light UBO
struct LightUniformBufferObject {
    alignas(16) glm::vec3 lightPos_Key; // 16 bytes
    alignas(16) glm::vec3 lightPos_Rim; // 16 bytes
    alignas(16) glm::vec3 lightAmbient; // 16 bytes  
}; // 48 bytes 

// SSAO Buffer
struct SSAOKernelBufferObject {
    alignas(16) glm::vec4 SSAOKernel[16]; 
};

