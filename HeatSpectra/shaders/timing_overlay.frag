#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 1) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

void main() {
    vec3 msdf = texture(fontAtlas, fragTexCoord).rgb;
    float distance = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));

    float pxRange = 8.0;
    vec2 atlasSize = vec2(textureSize(fontAtlas, 0));
    vec2 unitRange = vec2(pxRange) / atlasSize;
    vec2 screenTexSize = vec2(1.0) / fwidth(fragTexCoord);
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    float screenPxDistance = screenPxRange * (distance - 0.5);
    float sdfAlpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    float alpha = sdfAlpha * fragColor.a;
    if (alpha < 0.01) {
        discard;
    }

    outColor = vec4(fragColor.rgb, alpha);
}
