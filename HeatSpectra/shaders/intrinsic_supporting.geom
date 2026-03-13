#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(triangles) in;

layout(triangle_strip, max_vertices = 3) out;
layout(location = 4) out vec3 fragBaryCoord;

void main() {
    // Pass through gl_PrimitiveID
    gl_PrimitiveID = gl_PrimitiveIDIn;
    
    // Emit vertex 0
    gl_Position = gl_in[0].gl_Position;
    fragBaryCoord = vec3(1.0, 0.0, 0.0);
    EmitVertex();
    
    // Emit vertex 1
    gl_Position = gl_in[1].gl_Position;
    fragBaryCoord = vec3(0.0, 1.0, 0.0);
    EmitVertex();
    
    // Emit vertex 2
    gl_Position = gl_in[2].gl_Position;
    fragBaryCoord = vec3(0.0, 0.0, 1.0);
    EmitVertex();
    
    EndPrimitive();
}
