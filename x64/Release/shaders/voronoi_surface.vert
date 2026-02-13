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
    int _pad0;
    int _pad1;
    int _pad2;
} pc;

// Output to geometry shader
layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vModelPos;

void main() {
    // Use push constant model matrix (same as gbuffer.vert)
    vec4 worldPos = pc.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    // Transform normal to world space for lighting
    vNormal = normalize(mat3(pc.modelMatrix) * inNormal);
    
    // Pass world position for lighting calculations
    vWorldPos = worldPos.xyz;

    // Pass model-space position for Voronoi distance computations
    vModelPos = inPosition;
}
