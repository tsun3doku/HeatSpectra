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
    vec4 lightParams; // x=keyIntensity, y=rimIntensity, z=ambientIntensity
    vec3 cameraPos;
} lightUbo;

// Input attachments for Gbuffer data
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputAlbedo;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormal;
layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputPosition;

layout(location = 0) in vec2 inUV; 

// Output color
layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0) + 1.0, 1e-4);
    return a2 / max(PI * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-4);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 evaluateDirectionalBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, vec3 F0, vec3 radiance) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) {
        return vec3(0.0);
    }

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuse = (vec3(1.0) - F) * albedo * (1.0 / PI);

    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    // Read Gbuffer data
    vec4 albedoSample = subpassLoad(inputAlbedo);
    float coverage = clamp(albedoSample.a, 0.0, 1.0);
    if (coverage <= 0.0) {
        // No geometry coverage at this sample: keep the cleared lighting color.
        discard;
    }

    vec4 normalSample = subpassLoad(inputNormal);
    vec4 positionSample = subpassLoad(inputPosition);

    vec3 albedo = albedoSample.rgb;
    vec3 normal = normalize(normalSample.rgb);
    vec3 position = positionSample.rgb;
    float roughness = clamp(normalSample.a, 0.04, 1.0);
    float specularF0 = clamp(positionSample.a, 0.0, 1.0);

    vec3 viewDir = normalize(lightUbo.cameraPos - position);
    vec3 F0 = vec3(specularF0);

    vec3 keyDir = normalize(-lightUbo.lightPos_Key);
    vec3 rimDir = normalize(-lightUbo.lightPos_Rim);
    vec3 keyRadiance = vec3(max(lightUbo.lightParams.x, 0.0));
    vec3 rimRadiance = vec3(max(lightUbo.lightParams.y, 0.0));
    vec3 ambient = albedo * lightUbo.lightAmbient * max(lightUbo.lightParams.z, 0.0);

    vec3 direct = evaluateDirectionalBRDF(normal, viewDir, keyDir, albedo, roughness, F0, keyRadiance) +
                  evaluateDirectionalBRDF(normal, viewDir, rimDir, albedo, roughness, F0, rimRadiance);
    vec3 litColor = ambient + direct;

    // Silhouette coverage is handled by per-sample stencil in lighting subpass.
    fragColor = vec4(litColor, 1.0);
}
