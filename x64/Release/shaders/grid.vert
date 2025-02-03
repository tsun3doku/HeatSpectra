#version 450

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;  // View matrix
    mat4 proj;  // Projection matrix
    vec3 pos;   // Camera position (optional)
} viewUniforms;

layout(location = 1) out vec3 nearPoint;     // Near point output
layout(location = 2) out vec3 farPoint;      // Far point output
layout(location = 3) out mat4 fragView;      // View matrix output
layout(location = 7) out mat4 fragProj;      // Projection matrix output

// Grid positions in clipped space
vec3 gridPlane[6] = vec3[] (
    vec3(1, 1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
    vec3(-1, -1, 0), vec3(1, 1, 0), vec3(1, -1, 0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 view, mat4 projection) {
    mat4 viewInv = inverse(view);
    mat4 projInv = inverse(projection);
    vec4 unprojectedPoint = viewInv * projInv * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    vec3 p = gridPlane[gl_VertexIndex].xyz;

    // Unprojecting points
    nearPoint = UnprojectPoint(p.x, p.y, 0.0, viewUniforms.view, viewUniforms.proj);
    farPoint = UnprojectPoint(p.x, p.y, 1.0, viewUniforms.view, viewUniforms.proj);

    // Pass the view and projection matrices to the fragment shader
    fragView = viewUniforms.view;
    fragProj = viewUniforms.proj;

    // Set gl_Position for the vertex
    gl_Position = vec4(p, 1.0);
}
