#ifndef STRUCTS_HPP
#define STRUCTS_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//main UBO
struct UniformBufferObject {
    alignas(16) glm::mat4 model;  
    alignas(16) glm::mat4 view;  
    alignas(16) glm::mat4 proj;
   
};

//grid UBO
struct GridUniformBufferObject {
    glm::mat4 view; 
    glm::mat4 proj; 
    glm::vec3 pos; 
};

#endif