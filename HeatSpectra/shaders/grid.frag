#version 450

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 cameraPos;
layout(location = 1) out vec4 outColor;

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;
    mat4 proj;
    vec3 pos;
    vec3 gridSize;
} viewUniforms;

// Draw horizontal grid (XZ plane at y=0)
vec4 gridHorizontal(vec3 fragPos3D, float scale, bool drawAxis) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);

    float minimumZ = min(derivative.y, 1.0);
    float minimumX = min(derivative.x, 1.0);

    float greaterLineMargin = 0.15;
    float greaterScaleX = 0.2;
    float greaterScaleZ = 0.2;

    float greaterLineX = abs(fract(coord.y * greaterScaleX + 0.5) - 0.5) / derivative.y;
    float greaterLineZ = abs(fract(coord.x * greaterScaleZ + 0.5) - 0.5) / derivative.x;

    float axisMargin = .08;

    vec4 color = vec4(0.05, 0.05, 0.05, smoothstep(0.0, 1.5, (1.0 - min(line, 1.0))));

    if (greaterLineX < greaterLineMargin) {
        color = vec4(0.3, 0.3, 0.3, (1.0 - min(line, 0.6)));
    }

    if (greaterLineZ < greaterLineMargin) {
        color = vec4(0.3, 0.3, 0.3, (1.0 - min(line, 0.6)));
    }

    // Z-axis (blue)
    if(fragPos3D.x > -axisMargin * minimumX && fragPos3D.x < axisMargin * minimumX) {
        color = vec4(0.0, 0.05, 0.9, (1.0 - min(line, 0.6)));
    }
    // X-axis (red)
    if(fragPos3D.z > -axisMargin * minimumZ && fragPos3D.z < axisMargin * minimumZ) {
        color = vec4(0.9, 0.0, 0.05, (1.0 - min(line, 0.6)));
    }

    return color;
}

void main() {
    outColor = gridHorizontal(worldPos, 10, true);
}

