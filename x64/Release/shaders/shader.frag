#version 450
layout(set = 0, binding = 3) uniform sampler2D depthTexture;
layout(set = 0, binding = 4) uniform sampler2D normalTexture;	
layout(set = 0, binding = 5) buffer SSAOKernelBuffer {
    vec4 SSAOKernel[16];
};	

layout(location = 0) in vec3 fragColor; 
layout(location = 1) in vec3 fragPosition;  
layout(location = 2) in vec3 fragNormal;    
layout(location = 3) in vec2 fragTexCoord;  
layout(location = 4) in vec3 fragLightDir_Key; 
layout(location = 5) in vec3 fragLightDir_Rim;
layout(location = 6) in vec3 fragLightAmbient;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec3 gPosition;

const float lightIntensity_Rim = 5.0f;
const float lightIntensity_Key = 5.0f;
const float radius = 0.5f; // SSAO radius for sampling
const float bias = 0.025f; // Depth bias to avoid self-occlusion

/*float computeSSAO(vec3 fragPos, vec3 fragNormal, sampler2D depthTexture, sampler2D normalTexture, vec4 SSAOKernel[16]) {
    float occlusion = 0.0;
    for (int i = 0; i < 16; i++) {
        // Sample kernel
        vec4 Kernel = SSAOKernel[i];
        vec3 samplePos = fragPos + fragNormal * Kernel.xyz * radius;

        // Transform sample position to screen space
        vec4 clipSpacePos = vec4(samplePos, 1.0);
        vec3 projCoord = clipSpacePos.xyz / clipSpacePos.w;
        projCoord = projCoord * 0.5 + 0.5; // Convert to [0, 1] range
        
        // Sample depth and normal from the textures
        float sampleDepth = texture(depthTexture, projCoord.xy).r;
        vec3 sampleNormal = texture(normalTexture, projCoord.xy).xyz;

        // Compare depth to check if sample is occluded
        float sampleOcclusion = (abs(projCoord.z - sampleDepth) < bias) ? 1.0 : 0.0;
        
        // Apply dot product between normal and kernel sample to simulate occlusion
        float normalFactor = max(dot(sampleNormal, fragNormal), 0.0);
        occlusion += sampleOcclusion * normalFactor;
    }
    return occlusion / 16.0; // Normalize by the number of samples
}*/

void main() {
    // Compute Diffuse lighting for both Key and Rim lights
    //float diffuse_Key = max(dot(fragNormal, fragLightDir_Key), 0.0);
    //float diffuse_Rim = max(dot(fragNormal, fragLightDir_Rim), 0.0);

    //vec3 diffuseLighting_Key = diffuse_Key * fragColor * lightIntensity_Key;
    //vec3 diffuseLighting_Rim = diffuse_Rim * fragColor * lightIntensity_Rim;

    //float ssao = computeSSAO(fragPosition, fragNormal, depthTexture, normalTexture, SSAOKernel); //new

    // Combine with ambient light
    //vec3 totalLight = fragLightAmbient + diffuseLighting_Key + diffuseLighting_Rim;
    //totalLight *= (1.0 - ssao); //new
    
    // Apply ambient occlusion or other effects if needed
    //outColor = vec4(totalLight, 1.0);

    gAlbedo = vec4(fragColor, 1.0);
    gNormal = normalize(fragNormal);
    gPosition = fragPosition;
} 
