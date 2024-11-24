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
layout(location = 3) out vec3 fragTexture; 

vec3 lightDir_Key = normalize(lightUbo.lightPos_Key - vec3(ubo.model * vec4(inPosition, 1.0)));
vec3 lightDir_Rim = normalize(lightUbo.lightPos_Rim - vec3(ubo.model * vec4(inPosition, 1.0))); 
const float lightIntensity_Rim = 1.0f;
const float lightIntensity_Key = 2.0f;

vec3 normalWorldSpace = normalize(mat3(ubo.model) * inNormal);
vec3 Diffuse_KeyIntensity = pow(((max(dot(normalWorldSpace, lightDir_Key) , 0.0))/2) + (1/2) , 2.0) + ubo.color;
vec3 Diffuse_RimIntensity = (max(dot(normalWorldSpace, lightDir_Rim) , 0.0)) + ubo.color;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

    vec3 Diffuse_Key = (Diffuse_KeyIntensity * ubo.color) / max(ubo.color.r, max(ubo.color.g, ubo.color.b));
    vec3 Diffuse_Rim = (Diffuse_RimIntensity * ubo.color) / max(ubo.color.r, max(ubo.color.g, ubo.color.b));

    vec3 lightOut_Key = lightUbo.lightAmbient + Diffuse_Key * lightIntensity_Key;
    vec3 lightOut_Rim = lightUbo.lightAmbient * 0.1 + Diffuse_Rim * lightIntensity_Rim;

    vec3 totalLight = lightOut_Key + lightOut_Rim;

    fragColor = totalLight * inColor;
}