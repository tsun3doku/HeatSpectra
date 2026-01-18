#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;  
    mat4 proj;
    vec3 color;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    int useHeatColors;
} pc;

// Input attributes from buffers
layout(location = 0) in vec3 inPos;           // From Vertex buffer
layout(location = 1) in vec3 inColor;         // From Vertex buffer
layout(location = 2) in vec3 inNormal;        // From Vertex buffer
layout(location = 3) in vec2 inTexCoord;      // From Vertex buffer 

// Output to the fragment shader
layout(location = 0) out vec3 fragColor;         
layout(location = 1) out vec3 fragNormal; 
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec2 fragTexCoord;      
                 
void main() {
    // Always use regular vertex data - heat rendering uses separate pipeline
    vec3 worldPos = vec3(pc.modelMatrix * vec4(inPos, 1.0));
    // Transform normal to world space
    vec3 worldNormal = normalize(mat3(pc.modelMatrix) * inNormal);
    
    fragColor = ubo.color;          
    fragNormal = worldNormal;        
    fragTexCoord = inTexCoord;    
    fragPos = worldPos;          

    // Final clip-space position
    gl_Position = ubo.proj * ubo.view * pc.modelMatrix * vec4(inPos, 1.0);
}