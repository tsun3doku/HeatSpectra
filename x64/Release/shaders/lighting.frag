#version 450
#extension GL_KHR_vulkan_glsl:enable

layout(set = 0, binding = 4) uniform UniformBufferObject {
    mat4 model; 
    mat4 view;  
    mat4 proj;
    vec3 color;
} ubo;

layout(set = 0, binding = 5) uniform LightUniformBufferObject {
    vec3 lightPos_Key;
    vec3 lightPos_Rim;
    vec3 lightAmbient;
} lightUbo;

// Input attachments for G-buffer data
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputAlbedo;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormal;
layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputPosition;

layout(location = 0) in vec2 inUV; 

// Output color
layout(location = 0) out vec4 fragColor;

const float lightIntensity_Rim = 0.0f;
const float lightIntensity_Key = 5.0f;

float computeDiffuse(vec3 normal, vec3 lightDir) {
    return max(dot(normal, lightDir), 0.0);
}

void main() {
    // Read G-buffer data
    vec3 albedo = subpassLoad(inputAlbedo).rgb;  
    vec3 normal = normalize(subpassLoad(inputNormal).rgb); 
    vec3 position = subpassLoad(inputPosition).rgb;

    float diffuse_Key = computeDiffuse(normal, lightUbo.lightPos_Key);
    float diffuse_Rim = computeDiffuse(normal, lightUbo.lightPos_Rim);
    vec3 ambientLight = lightUbo.lightAmbient;

    vec3 diffuseLighting_Key = diffuse_Key * albedo * lightIntensity_Key;
    vec3 diffuseLighting_Rim = diffuse_Rim * albedo * lightIntensity_Rim;

    vec3 totalLighting = ambientLight + diffuseLighting_Key + diffuseLighting_Rim;
    float shadingStrength = 0.1;
    vec3 finalColor = mix(albedo, totalLighting, shadingStrength);

    // Final fragment color
    fragColor = vec4(finalColor, 1.0);  // Output the calculated lighting
}
