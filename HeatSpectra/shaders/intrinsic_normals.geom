#version 450

layout(points) in;
layout(line_strip, max_vertices = 2) out;

layout(location = 0) in vec3 inCenter[];
layout(location = 1) in vec3 inNormal[];
layout(location = 2) in float inArea[];

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float normalLength;
    float avgArea;
} push;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

void main() {
    vec3 center = inCenter[0];
    vec3 normal = inNormal[0];
    float area = inArea[0];
    
    // Transform center to world space
    vec4 worldCenter = push.modelMatrix * vec4(center, 1.0);
    
    // Transform normal direction 
    vec3 worldNormal = mat3(push.modelMatrix) * normal;
    worldNormal = normalize(worldNormal);
    
    // Calculate normal endpoint
    vec4 worldEnd = worldCenter + vec4(worldNormal * push.normalLength, 0.0);
    
    // Color based on deviation from average area
    // Close to average = Blue (uniform mesh)
    // Far from average = Red (outlier/non-uniform)
    
    float deviation = abs(area - push.avgArea) / max(push.avgArea, 0.0001);
    
    // Map deviation to color: 0 = blue (at average), 1+ = red (far from average)
    // Scale deviation so that 2x or 0.5x average is maximum red
    float deviationFactor = clamp(deviation * 2.0, 0.0, 1.0);
    
    // Gradient: Blue (uniform) -> Cyan -> Green -> Yellow -> Red (outlier)
    vec3 normalColor;
    if (deviationFactor < 0.25) {
        // Blue to Cyan (very close to average)
        normalColor = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), deviationFactor * 4.0);
    } else if (deviationFactor < 0.5) {
        // Cyan to Green (close to average)
        normalColor = mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), (deviationFactor - 0.25) * 4.0);
    } else if (deviationFactor < 0.75) {
        // Green to Yellow (moderate deviation)
        normalColor = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (deviationFactor - 0.5) * 4.0);
    } else {
        // Yellow to Red (large deviation)
        normalColor = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (deviationFactor - 0.75) * 4.0);
    }
    
    // Emit base point
    gl_Position = ubo.proj * ubo.view * worldCenter;
    outColor = normalColor;
    EmitVertex();
    
    // Emit tip of normal
    gl_Position = ubo.proj * ubo.view * worldEnd;
    outColor = normalColor;
    EmitVertex();
    
    EndPrimitive();
}
