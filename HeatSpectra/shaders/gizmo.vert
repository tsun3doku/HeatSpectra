#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
    float hovered;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
    
    if (pc.hovered > 0.5) {
        fragColor = pc.color * 1.5;
    } else {
        fragColor = pc.color;
    }
}
