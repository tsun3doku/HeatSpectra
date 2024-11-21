#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
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
const float lightIntensity = 2.0f;


void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    vec3 normalWorldSpace = normalize(mat3(ubo.model) * inNormal);
    float Diffuse_Key = pow(((max(dot(normalWorldSpace, lightDir_Key) , 0.0))/2) + (1/2) , 2.0);
    float Diffuse_Rim = (max(dot(normalWorldSpace, lightDir_Rim) , 0.0));

    vec3 lightOut_Key = lightUbo.lightAmbient + Diffuse_Key * lightIntensity;
    vec3 lightOut_Rim = lightUbo.lightAmbient + Diffuse_Rim * lightIntensity;

    vec3 totalLight = lightOut_Key + lightOut_Rim;

    fragColor = lightOut_Rim * inColor;
}