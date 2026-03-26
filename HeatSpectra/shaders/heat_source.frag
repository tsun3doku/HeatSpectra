#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    mat4 view;
    mat4 proj;
    vec4 sourceParams; // x = temperature, y = isSource
} push;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;

vec3 temperatureToColor(float t) {
    if (t <= 0.0) return vec3(0, 0, 0);
    else if (t < 0.25) return mix(vec3(0, 0, 0), vec3(0.1, 0.0, 0.6), t * 4.0);
    else if (t < 0.375) return mix(vec3(0.1, 0.0, 0.6), vec3(0.3, 0.0, 0.5), (t - 0.25) * 8.0);
    else if (t < 0.55) return mix(vec3(0.3, 0.0, 0.5), vec3(0.9, 0.0, 0.0), (t - 0.375) * 5.71);
    else if (t < 0.75) return mix(vec3(0.9, 0.0, 0.0), vec3(0.9, 0.6, 0.0), (t - 0.55) * 5.0);
    else if (t < 0.9) return mix(vec3(0.9, 0.6, 0.0), vec3(1.0, 1.0, 0.3), (t - 0.75) * 6.67);
    else return mix(vec3(1.0, 1.0, 0.3), vec3(1.0, 1.0, 1.0), (t - 0.9) * 10.0);
}

void main() {
    float temperatureScale = 50.0;
    float normalized = clamp(push.sourceParams.x / temperatureScale, 0.0, 1.0);
    vec3 heatColor = temperatureToColor(normalized);

    gAlbedo = vec4(heatColor, 1.0);
    gNormal = vec4(normalize(fragNormal), 0.0);
}
