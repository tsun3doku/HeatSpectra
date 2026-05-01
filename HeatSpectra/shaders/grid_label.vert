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
layout(set = 0, binding = 1) uniform sampler2D fontAtlas;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float outAlpha;

void main() {
    vec3 right = instanceRightVec;
    vec3 up = instanceUpVec;
    vec3 currentPos = instancePos;
    outAlpha = 1.0;
    
    // Build Quad
    // Calculate world height and width based on character aspect ratio
    float quadHeightWorld = instanceScale;
    
    vec2 atlasSize = vec2(textureSize(fontAtlas, 0));
    float quadWidthWorld = quadHeightWorld * (instanceCharUV.z / instanceCharUV.w) * (atlasSize.x / atlasSize.y);
    
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
