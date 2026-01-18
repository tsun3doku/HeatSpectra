#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(triangles) in;
layout(location = 0) in vec3 inFragPosition[];
layout(location = 1) in vec3 inFragNormal[];
layout(location = 2) in vec3 inFragColor[];
layout(location = 3) in vec2 inFragTexCoord[];

layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out vec3 fragBaryCoord;
layout(location = 5) out vec2 fragIntrinsicCoord;  

void main() {
    // World space positions 
    vec3 p0_3D = inFragPosition[0];
    vec3 p1_3D = inFragPosition[1];
    vec3 p2_3D = inFragPosition[2];
    
    // Calculate two edge vectors
    vec3 u = p1_3D - p0_3D;
    vec3 v = p2_3D - p0_3D;
    
    // Build orthonormal basis for the triangle's plane
    vec3 normal = normalize(cross(u, v));
    vec3 tangent = normalize(u);
    vec3 bitangent = cross(normal, tangent);
    
    // Create projection matrix from 3D to 2D 
    mat3 basis = transpose(mat3(tangent, bitangent, normal));
    
    // Project 3D points onto 2D plane using the orthonormal basis
    // p0 is at the origin
    vec2 p0_2D = vec2(0.0, 0.0);
    // p1 lies along the x-axis 
    vec2 p1_2D = vec2(length(u), 0.0);
    // p2 is projected using the basis
    vec2 p2_2D = (basis * v).xy;
    
    // Pass through gl_PrimitiveID
    gl_PrimitiveID = gl_PrimitiveIDIn;
    
    // Emit vertex 0
    gl_Position = gl_in[0].gl_Position;
    fragPosition = inFragPosition[0];
    fragNormal = inFragNormal[0];
    fragColor = inFragColor[0];
    fragTexCoord = inFragTexCoord[0];
    fragBaryCoord = vec3(1.0, 0.0, 0.0);
    fragIntrinsicCoord = p0_2D;
    EmitVertex();
    
    // Emit vertex 1
    gl_Position = gl_in[1].gl_Position;
    fragPosition = inFragPosition[1];
    fragNormal = inFragNormal[1];
    fragColor = inFragColor[1];
    fragTexCoord = inFragTexCoord[1];
    fragBaryCoord = vec3(0.0, 1.0, 0.0);
    fragIntrinsicCoord = p1_2D;
    EmitVertex();
    
    // Emit vertex 2
    gl_Position = gl_in[2].gl_Position;
    fragPosition = inFragPosition[2];
    fragNormal = inFragNormal[2];
    fragColor = inFragColor[2];
    fragTexCoord = inFragTexCoord[2];
    fragBaryCoord = vec3(0.0, 0.0, 1.0);
    fragIntrinsicCoord = p2_2D;
    EmitVertex();
    
    EndPrimitive();
}
