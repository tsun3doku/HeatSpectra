#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 aPos;  

struct SurfacePoint {
    vec3 position;      
    float temperature;    
    vec3 normal;        
    float area;         
    vec4 color;         
};

layout(binding = 0) buffer HeatSourceBuffer {
    SurfacePoint surfacePoints[];
};

layout(binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(binding = 2) uniform DebugParams {
    mat4 modelMatrix;
    float surfelRadius;
} debugParams;

layout(location = 0) out vec4 outColor;

void main() {
    uint surfelIndex = gl_InstanceIndex;
    
    if (surfelIndex >= surfacePoints.length()) {
        gl_Position = vec4(0.0, 0.0, -10.0, 1.0);
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    
    // Get surfel from surface points
    vec3 surfelPosModel = surfacePoints[surfelIndex].position;
    vec3 surfelNormalModel = surfacePoints[surfelIndex].normal;
    float area = surfacePoints[surfelIndex].area;
    float temperature = surfacePoints[surfelIndex].temperature;
    
    // Transform to world space
    vec3 surfelPosWorld = (debugParams.modelMatrix * vec4(surfelPosModel, 1.0)).xyz;
    vec3 surfelNormalWorld = normalize(mat3(debugParams.modelMatrix) * surfelNormalModel);
    
    float surfelRadius = debugParams.surfelRadius;
    
    // Create tangent vectors for the disc
    vec3 up = abs(surfelNormalWorld.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent1 = normalize(cross(surfelNormalWorld, up));
    vec3 tangent2 = normalize(cross(surfelNormalWorld, tangent1));
    
    // Position this vertex on the circle
    vec3 localPos = tangent1 * aPos.x * surfelRadius + tangent2 * aPos.y * surfelRadius;
    vec3 worldPos = surfelPosWorld + localPos;
    
    // Project to screen using UBO matrices
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Color based on temperature
    float normalizedTemp = clamp((temperature - 20.0) / 100.0, 0.0, 1.0);

    vec3 color = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), normalizedTemp);
    outColor = vec4(color, 0.6); 
}
