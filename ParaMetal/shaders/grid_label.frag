#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float inAlpha;
layout(location = 2) in vec4 fragFillColor;
layout(location = 3) flat in vec4 fragUvRect;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D fontAtlas;

float medianDistance(vec3 msdf) {
    return max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
}

float glyphCoverage(vec2 uv, float screenPxRange, vec2 halfTexel) {
    vec2 uvMin = fragUvRect.xy + halfTexel;
    vec2 uvMax = fragUvRect.xy + fragUvRect.zw - halfTexel;
    const float msdfLodBias = -0.75;
    float distance = medianDistance(texture(fontAtlas, clamp(uv, uvMin, uvMax), msdfLodBias).rgb);
    return clamp(screenPxRange * (distance - 0.5) + 0.5, 0.0, 1.0);
}

void main() {
    // Pixel range used when generating the SDF and must match distanceRange in JSON
    float pxRange = 8.0;
    
    vec2 atlasSize = vec2(textureSize(fontAtlas, 0));
    
    // How far in UV space to go from min to max SDF value
    vec2 unitRange = vec2(pxRange) / atlasSize;
    
    // Texture size in screen pixels
    vec2 screenTexSize = vec2(1.0) / fwidth(fragTexCoord);
    
    // SDF range in output screen pixels
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    
    vec2 halfTexel = 0.5 / atlasSize;
    float fillAlpha = glyphCoverage(fragTexCoord, screenPxRange, halfTexel);

    // Creat outline
    float outlineRadiusPx = mix(0.35, 0.80, smoothstep(1.0, 4.0, screenPxRange));
    vec2 pixelX = dFdx(fragTexCoord) * outlineRadiusPx;
    vec2 pixelY = dFdy(fragTexCoord) * outlineRadiusPx;
    const float diagonal = 0.70710678;
    float outlineAlpha = fillAlpha;
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord + pixelX, screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord - pixelX, screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord + pixelY, screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord - pixelY, screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord + diagonal * (pixelX + pixelY), screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord + diagonal * (pixelX - pixelY), screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord - diagonal * (pixelX - pixelY), screenPxRange, halfTexel));
    outlineAlpha = max(outlineAlpha, glyphCoverage(fragTexCoord - diagonal * (pixelX + pixelY), screenPxRange, halfTexel));

    float outlineOnlyAlpha = max(outlineAlpha - fillAlpha, 0.0);
    vec3 outlineColor = vec3(0.02, 0.025, 0.03);
    vec3 premultipliedColor = outlineColor * outlineOnlyAlpha + fragFillColor.rgb * fillAlpha;
    vec3 finalColor = premultipliedColor / max(outlineAlpha, 0.0001);
    float finalAlpha = outlineAlpha * fragFillColor.a * inAlpha;

    outColor = vec4(finalColor, finalAlpha);
    
    if (finalAlpha < 0.01) 
    discard;
}
