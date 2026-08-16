#version 450
#extension GL_KHR_vulkan_glsl:enable

layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput lineOverlayInput;
layout(input_attachment_index=1, set=0, binding=1) uniform subpassInput lightingInput;
layout(input_attachment_index=2, set=0, binding=2) uniform subpassInput albedoCoverageInput;
layout(set=0, binding=3) uniform sampler2D backgroundTexture;
layout(push_constant) uniform BlendSettings {
    uint backgroundMode;
} settings;
layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

vec3 linearToSrgb(vec3 c)
{
    vec3 clamped = max(c, vec3(0.0));
    vec3 low = 12.92 * clamped;
    vec3 high = 1.055 * pow(clamped, vec3(1.0 / 2.4)) - 0.055;
    bvec3 useLow = lessThanEqual(clamped, vec3(0.0031308));
    return vec3(
        useLow.x ? low.x : high.x,
        useLow.y ? low.y : high.y,
        useLow.z ? low.z : high.z);
}

void main() {
    vec4 lighting = subpassLoad(lightingInput);
    vec4 lineOverlay = subpassLoad(lineOverlayInput);

    vec3 clearColor = settings.backgroundMode == 0u
        ? texture(backgroundTexture, inUV).rgb
        : vec3(1.0);

    float coverage = clamp(lighting.a, 0.0, 1.0);
    vec3 color = lighting.rgb + clearColor.rgb * (1.0 - coverage);
    color = color * (1.0 - lineOverlay.a) + lineOverlay.rgb;

    outColor = vec4(linearToSrgb(color), 1.0);
}
