#version 450

layout(set=0, binding=0) uniform UniformBufferObject { 
mat4 model; 
mat4 view; 
mat4 proj;
vec3 color;
} ubo;

layout(location=0) in vec3 inPos;
layout(location=0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
    fragColor = ubo.color;
}