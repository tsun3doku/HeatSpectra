#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(location = 0) in vec3 fragColor;     // Color passed from vertex shader
layout(location = 1) in vec3 fragPosition;  // Position in world space passed from vertex shader
layout(location = 2) in vec3 fragNormal;    // Normal passed from vertex shader
layout(location = 3) in vec3 fragTexture;       // Texture passed from vertex shader

layout(location = 0) out vec4 outColor; 

void main() {

   outColor = vec4(fragColor, 1.0);
}