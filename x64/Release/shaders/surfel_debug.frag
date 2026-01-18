#version 450

layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 color;

void main() {
    // For triangle-based circles, we don't need gl_PointCoord
    // The vertex shader already creates proper circular geometry
    
    // Pass through the color and alpha from vertex shader
    color = inColor;
}
