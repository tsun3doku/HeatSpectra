#version 450

layout(location = 0) in vec3 inPosition;  // Quad corner
layout(location = 1) in vec2 inTexCoord;  // UV coordinates for this corner

// Per instance
layout(location = 2) in vec3 instancePos;       // World position of character center
layout(location = 3) in vec4 instanceCharUV;    // UV rect for character in atlas
layout(location = 4) in float instanceScale;    // Size of the label
layout(location = 5) in vec3 instanceRightVec;  // Orientation vector
layout(location = 6) in vec3 instanceUpVec;     // Orientation vector

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    vec3 gridSize;
} viewUniforms;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float outAlpha;

void main() {
    
    vec3 right;
    vec3 up;
    vec3 currentPos = instancePos;
    outAlpha = 1.0;  
    
    // Billboard condition (Y-Axis labels)
    if (instanceUpVec.y > 0.9) {
        up = vec3(0.0, 1.0, 0.0);
        
        // Calculate billboard rotation 
        vec3 dir = vec3(-viewUniforms.view[0][2], 0.0, -viewUniforms.view[2][2]);
        float dirLen = length(dir);
        dir = (dirLen < 0.001) ? vec3(0.0, 0.0, -1.0) : normalize(dir);
        right = normalize(cross(dir, up));
        right = normalize(cross(dir, up));
        right = normalize(cross(dir, up));
        currentPos += right * instanceRightVec.x + up * instanceRightVec.y;
              
        // Direction from grid Ccnter to pillar 
        vec3 pillarDir = normalize(vec3(instancePos.x, 0.0, instancePos.z));
        
        // Direction from grid center to camera
        vec3 camDir = vec3(viewUniforms.cameraPos.x, 0.0, viewUniforms.cameraPos.z);
        
        // Safety check for camera being exactly at center (0,0)
        if (length(camDir) > 0.001) {
            camDir = normalize(camDir);
            
            // Calculate visibility based on pillar direction relative to camera
            float backFactor = -dot(pillarDir, camDir);
            outAlpha = smoothstep(0.5, 0.85, backFactor);
        }
    } 
    else {
        // Floor labels
        right = instanceRightVec;
        up = instanceUpVec;
    }
    
    // Build Quad
    // Calculate world height and width based on character aspect ratio
    float quadHeightWorld = instanceScale;
    
    float quadWidthWorld = quadHeightWorld * (instanceCharUV.z / instanceCharUV.w) * (856.0 / 64.0);
    
    vec3 worldPos = currentPos + 
                    (right * inPosition.x * quadWidthWorld) +
                    (up * inPosition.y * quadHeightWorld);
    
    // Calculate camera graze angle 
    // The normal to the quad is perpendicular to both right and up vectors
    vec3 quadNormal = normalize(cross(right, up));
    
    // Direction from quad to camera
    vec3 toCam = normalize(viewUniforms.cameraPos - worldPos);
    
    float grazeAngle = abs(dot(quadNormal, toCam));
    
    // Start fading at 0.25, fade out at 0.1
    float grazeFade = smoothstep(0.1, 0.25, grazeAngle);
    outAlpha *= grazeFade;
    
    gl_Position = viewUniforms.proj * viewUniforms.view * vec4(worldPos, 1.0);
    fragTexCoord = instanceCharUV.xy + inTexCoord * instanceCharUV.zw;
}
