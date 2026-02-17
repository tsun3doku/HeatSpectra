#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inCenterPx;
layout(location = 3) in vec2 inSizePx;
layout(location = 4) in vec4 inCharUV;
layout(location = 5) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
} pushConstants;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    vec2 pixelPos = inCenterPx + (inPosition * inSizePx);
    vec2 ndcPos;
    ndcPos.x = (pixelPos.x / pushConstants.viewportSize.x) * 2.0 - 1.0;
    ndcPos.y = 1.0 - ((pixelPos.y / pushConstants.viewportSize.y) * 2.0);

    gl_Position = vec4(ndcPos, 0.0, 1.0);
    fragTexCoord = inCharUV.xy + (inTexCoord * inCharUV.zw);
    fragColor = inColor;
}
