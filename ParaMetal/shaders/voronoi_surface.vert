#version 450

// Standard vertex attributes from Model
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

// Camera UBO (view/proj only, model comes from push constant)
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;      // Unused - kept for compatibility
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

// Push constant for per-draw model matrix (matches gbuffer.vert)
layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float alpha;
    float canonicalToWorldScale;
    float _pad0;
    float _pad1;
} pc;

// Output to geometry shader
layout(location = 2) out vec3 vModelPosition;
layout(location = 4) out vec3 vModelNormal;
layout(location = 5) out vec3 vWorldPosition;

void main() {
    vec3 sourcePosition = inPosition;
    vModelPosition = sourcePosition;
    vWorldPosition = (pc.modelMatrix * vec4(sourcePosition, 1.0)).xyz;
    gl_Position = ubo.proj * ubo.view * vec4(vWorldPosition, 1.0);
    vModelNormal = inNormal;
}
