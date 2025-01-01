#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(set = 0, binding = 2) uniform LightUniformBufferObject {
    vec3 lightPos_Key;
    vec3 lightPos_Rim;
    vec3 lightAmbient;
} lightUbo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosition;  
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec2 fragTexCoord; 
layout(location = 4) out vec3 fragLightDir_Key;
layout(location = 5) out vec3 fragLightDir_Rim;
layout(location = 6) out vec3 fragLightAmbient;

void main() {
    vec3 worldPos = vec3(ubo.model * vec4(inPosition, 1.0));
    fragPosition = worldPos;
    fragColor = ubo.color;
    fragNormal = normalize(mat3(ubo.model) * inNormal);
    fragTexCoord = inTexCoord;
    fragLightAmbient = lightUbo.lightAmbient;

    fragLightDir_Key = normalize(lightUbo.lightPos_Key - worldPos);
    fragLightDir_Rim = normalize(lightUbo.lightPos_Rim - worldPos);

    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
}