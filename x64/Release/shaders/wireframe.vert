#version 450

layout(set=0, binding=0) uniform UniformBufferObject { 
mat4 model; 
mat4 view; 
mat4 proj;
vec3 color;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    int useHeatColors;
} pc;

layout(location=0) in vec3 inPos;
layout(location=0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * pc.modelMatrix * vec4(inPos, 1.0);
    fragColor = vec3(pow(0.000000,2.2), pow(0.478431,2.2), pow(0.800000,2.2)); // sRGB to linear approx
}