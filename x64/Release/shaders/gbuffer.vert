#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;  
    mat4 proj;
    vec3 color;
} ubo;

// Input attributes from buffers
layout(location = 0) in vec3 inPos;           // From Vertex buffer
layout(location = 1) in vec3 inColor;         // From Vertex buffer
layout(location = 2) in vec3 inNormal;        // From Vertex buffer
layout(location = 3) in vec2 inTexCoord;      // From Vertex buffer
layout(location = 4) in vec4 inSurfacePos;    // From Surface buffer 
layout(location = 5) in vec4 inSurfaceColor;  // From Surface buffer 

// Output to the fragment shader
layout(location = 0) out vec3 fragColor;         
layout(location = 1) out vec3 fragNormal; 
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec2 fragTexCoord;      
                 
void main() {
    vec3 worldPos = vec3(ubo.model * vec4(inSurfacePos.xyz, 1.0));
    fragColor = inSurfaceColor.xyz;          
    fragNormal = inNormal;        
    fragTexCoord = inTexCoord;    
    fragPos = worldPos;          

    // Final clip-space position
    gl_Position = ubo.proj * ubo.view * ubo.model * inSurfacePos;
}