#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 2) in vec3 vModelPosition[];
layout(location = 4) in vec3 vModelNormal[];
layout(location = 5) in vec3 vWorldPosition[];

layout(location = 2) out vec3 gModelPosition;
layout(location = 3) out vec2 gIntrinsicCoord;
layout(location = 4) out vec3 gModelNormal;
layout(location = 5) out vec3 gWorldPosition;

void main() {
    gl_PrimitiveID = gl_PrimitiveIDIn;

    vec3 p0 = vModelPosition[0];
    vec3 p1 = vModelPosition[1];
    vec3 p2 = vModelPosition[2];

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
        gModelPosition = vModelPosition[i];
        gIntrinsicCoord = (i == 0) ? p0_2D : (i == 1) ? p1_2D : p2_2D;
        gModelNormal = vModelNormal[i];
        gWorldPosition = vWorldPosition[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
