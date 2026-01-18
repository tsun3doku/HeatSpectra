#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float inAlpha;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D fontAtlas;

void main() {
    vec3 msdf = texture(fontAtlas, fragTexCoord).rgb;
    
    // Compute median of RGB channels for the signed distance
    float distance = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
    
    // Pixel range used when generating the SDF and must match distanceRange in JSON
    float pxRange = 8.0;
    
    vec2 textureSize = vec2(856.0, 64.0);
    
    // How far in UV space to go from min to max SDF value
    vec2 unitRange = vec2(pxRange) / textureSize;
    
    // Texture size in screen pixels
    vec2 screenTexSize = vec2(1.0) / fwidth(fragTexCoord);
    
    // SDF range in output screen pixels
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    
    // Signed distance in screen pixels
    float screenPxDistance = screenPxRange * (distance - 0.5);
    
    // Convert to alpha
    float sdfAlpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    
    vec3 textColor = vec3(0.9, 0.9, 0.9);
    
    float finalAlpha = sdfAlpha * inAlpha;

    outColor = vec4(textColor, finalAlpha);
    
    if (finalAlpha < 0.01) 
    discard;
}
