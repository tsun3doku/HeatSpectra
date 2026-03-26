#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    mat4 view;
    mat4 proj;
    vec4 sourceParams;
} push;

void main() {
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);

    fragPosition = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(push.modelMatrix))) * inNormal;
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    gl_Position = push.proj * push.view * worldPos;
}
