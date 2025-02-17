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

vec3 rgb2hsl(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    vec3 e = vec3(d, 1e-10, 1e-10);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e.y)), d / (q.x + e.z), q.x);
}

vec3 hsl2rgb(vec3 c) {
    vec3 p = abs(fract(c.x + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - vec3(3.0));
    return c.z * mix(vec3(1.0), clamp(p - vec3(1.0), 0.0, 1.0), c.y);
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

    vec3 hsl = rgb2hsl(albedo);
    hsl.z *= (1.0 + log(1.0 + dot(totalLighting, vec3(0.333))) * 0.5);

    vec3 finalColor = hsl2rgb(hsl);

    // Final fragment color
    fragColor = vec4(finalColor, 1.0);  // Output the calculated lighting
}
