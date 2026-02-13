#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) out vec4 outColor;

void main() {
    // Make circular points by discarding corners
    vec2 coord = gl_PointCoord - vec2(0.5);
    if (dot(coord, coord) > 0.25) 
        discard;
    
    outColor = vec4(fragColor, 1.0);
}
