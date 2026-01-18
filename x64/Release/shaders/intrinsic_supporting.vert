#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outFragPosition;
layout(location = 1) out vec3 outFragNormal;
layout(location = 2) out vec3 outFragColor;
layout(location = 3) out vec2 outFragTexCoord;

// Push constant for model matrix (updated per draw call)
layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
} push;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // Not used, kept for compatibility
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

void main() {
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    
    outFragPosition = worldPos.xyz;
    outFragNormal = mat3(transpose(inverse(push.modelMatrix))) * inNormal;
    outFragColor = inColor;
    outFragTexCoord = inTexCoord;
    
    gl_Position = ubo.proj * ubo.view * worldPos;
}
