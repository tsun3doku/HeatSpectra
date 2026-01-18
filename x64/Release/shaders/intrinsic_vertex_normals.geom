#version 450

layout(points) in;
layout(line_strip, max_vertices = 2) out;

layout(location = 0) in vec3 inPosition[];
layout(location = 1) in vec3 inNormal[];

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float normalLength;
    float avgArea;  // Not used for vertex normals, but kept for consistency
} push;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

void main() {
    vec3 position = inPosition[0];
    vec3 normal = inNormal[0];
    
    // Transform position to world space
    vec4 worldPosition = push.modelMatrix * vec4(position, 1.0);
    
    // Transform normal direction 
    vec3 worldNormal = mat3(push.modelMatrix) * normal;
    worldNormal = normalize(worldNormal);
    
    // Calculate normal endpoint
    vec4 worldEnd = worldPosition + vec4(worldNormal * push.normalLength, 0.0);
    
    // Color vertex normals differently from triangle normals
    // Use a distinctive color scheme: Purple to Magenta based on normal direction
    // This helps distinguish vertex normals from triangle normals visually
    
    // Map normal direction to color - use the Y component to vary color
    float normalY = clamp((worldNormal.y + 1.0) * 0.5, 0.0, 1.0);
    
    // Gradient: Purple (pointing down) -> Magenta (pointing up)
    vec3 normalColor = mix(
        vec3(0.5, 0.0, 0.8),  // Purple for normals pointing down
        vec3(1.0, 0.0, 1.0),  // Magenta for normals pointing up
        normalY
    );
    
    // Emit base point (vertex position)
    gl_Position = ubo.proj * ubo.view * worldPosition;
    outColor = normalColor;
    EmitVertex();
    
    // Emit tip of normal
    gl_Position = ubo.proj * ubo.view * worldEnd;
    outColor = normalColor;
    EmitVertex();
    
    EndPrimitive();
}
