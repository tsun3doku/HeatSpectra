#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float pointSize;       // World-space size (e.g., 0.02 = 2cm diameter)
    float viewportHeight;  // Actual viewport height in pixels
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    vec4 worldPos = pc.modelMatrix * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    gl_Position = ubo.proj * viewPos;
    
    // Calculate point size based on distance from camera
    // Use clip-space w (which equals -viewPos.z for perspective projection)
    float distance = gl_Position.w;
    
    // proj[1][1] = cot(fov/2), gives vertical scale factor
    // NOTE: Vulkan Y-flip makes this negative, so use abs()
    // Formula: screenSize = (worldSize / distance) * projScale * (viewportHeight / 2)
    float projScale = abs(ubo.proj[1][1]);
    float screenSize = (pc.pointSize / distance) * projScale * (pc.viewportHeight * 0.5);
    
    // Clamp to reasonable range
    gl_PointSize = clamp(screenSize, 1.0, 64.0);
    
    fragColor = inColor;
}


