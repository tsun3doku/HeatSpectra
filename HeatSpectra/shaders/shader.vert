#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosition;  
layout(location = 2) out vec3 fragNormal;  

const vec3 lightDir = normalize(vec3(1.0, 3.0, -1.0));
const float lightAmbient = 0.05;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    vec3 normalWorldSpace = normalize(mat3(ubo.model) * inNormal);

    float lightIntensity = lightAmbient + max(dot(normalWorldSpace, lightDir) , 0.0);

    fragColor = lightIntensity * inColor;
}