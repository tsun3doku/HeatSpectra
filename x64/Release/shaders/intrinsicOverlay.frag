#version 450

layout(location = 0) in vec3 fragColor;

// Deferred rendering G-buffer outputs
layout(location = 0) out vec4 outAlbedo;   // Albedo/color
layout(location = 1) out vec4 outNormal;   // Normal (not used for overlay)
layout(location = 2) out vec4 outPosition; // Position (not used for overlay)

void main() {
    // Write the per-vertex color to the albedo buffer
    outAlbedo = vec4(fragColor, 1.0);
    
    // For overlay, we don't need meaningful normals/positions
    // Just write dummy values
    outNormal = vec4(0.0, 1.0, 0.0, 1.0);
    outPosition = vec4(0.0, 0.0, 0.0, 1.0);
}