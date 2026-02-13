#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 vNormal[];
layout(location = 1) in vec3 vWorldPos[];
layout(location = 2) in vec3 vModelPos[];

layout(location = 0) out vec3 gNormal;
layout(location = 1) out vec3 gWorldPos;
layout(location = 2) out vec3 gModelPos;
layout(location = 3) out vec2 gIntrinsicCoord;

void main() {
    gl_PrimitiveID = gl_PrimitiveIDIn;

    // Build local 2D coordinates for the extrinsic triangle (model space)
    vec3 p0 = vModelPos[0];
    vec3 p1 = vModelPos[1];
    vec3 p2 = vModelPos[2];

    vec3 u = p1 - p0;
    vec3 v = p2 - p0;

    vec3 normal = normalize(cross(u, v));
    vec3 tangent = normalize(u);
    vec3 bitangent = cross(normal, tangent);

    mat3 basis = transpose(mat3(tangent, bitangent, normal));

    vec2 p0_2D = vec2(0.0, 0.0);
    vec2 p1_2D = vec2(length(u), 0.0);
    vec2 p2_2D = (basis * v).xy;

    for (int i = 0; i < 3; ++i) {
        gNormal = vNormal[i];
        gWorldPos = vWorldPos[i];
        gModelPos = vModelPos[i];
        gIntrinsicCoord = (i == 0) ? p0_2D : (i == 1) ? p1_2D : p2_2D;
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
