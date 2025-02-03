#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;  
    mat4 proj;
    vec3 color;
} ubo;

// Inputs from the vertex shader
layout(location = 0) in vec3 fragColor;       
layout(location = 1) in vec3 fragNormal;          
layout(location = 2) in vec3 fragPos;    // World position
layout(location = 3) in vec2 fragTexCoord;

// Outputs to the gbuffer
layout(location = 0) out vec4 gAlbedo;   
layout(location = 1) out vec4 gNormal;   
layout(location = 2) out vec4 gPosition;

void main() {
    // Write outputs to the gbuffer attachments
    gAlbedo = vec4(fragColor, 1.0);                  
    gNormal = vec4(normalize(fragNormal), 0.0);     
    gPosition = vec4(fragPos, 1.0);  
}