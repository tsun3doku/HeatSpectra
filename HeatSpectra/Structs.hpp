#ifndef STRUCTS_HPP
#define STRUCTS_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Main UBO
struct UniformBufferObject {
    alignas(16) glm::mat4 model;  
    alignas(16) glm::mat4 view;  
    alignas(16) glm::mat4 proj;
   
};

// Grid UBO
struct GridUniformBufferObject {
    alignas(16) glm::mat4 view; 
    alignas(16) glm::mat4 proj; 
    alignas(16) glm::vec3 pos; 

};

// Light UBO
struct LightUniformBufferObject {
    alignas(16) glm::vec3 lightPos_Key;
    alignas(16) glm::vec3 lightPos_Rim;
    alignas(16) glm::vec3 lightAmbient;
    
};

#endif