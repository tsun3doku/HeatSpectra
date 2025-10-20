#version 450

layout(set=0, binding=0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 translationOffset;
} push;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

void main() {
    vec3 worldPos = inPos + push.translationOffset;
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);
    fragColor = inColor;
}