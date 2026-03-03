#version 450

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D srcTexture;

void main()
{
    fragColor = vec4(texture(srcTexture, v_texcoord).rgb, 1.0);
}
