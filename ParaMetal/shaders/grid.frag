#version 450

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 cameraPos;
layout(location = 0) out vec4 outColor;

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

    vec2 majorCoord = coord / 5.0;
    vec2 majorDerivative = fwidth(majorCoord);
    vec2 majorGrid = abs(fract(majorCoord - 0.5) - 0.5) / majorDerivative;
    float majorLine = min(majorGrid.x, majorGrid.y);

    vec4 color = vec4(0.05, 0.05, 0.05, smoothstep(0.0, 1.5, (1.0 - min(line, 1.0))));

    if (majorLine < 0.8) {
        color = vec4(0.3, 0.3, 0.3, 1.0 - min(majorLine / 0.8, 0.75));
    }

    // Keep both axes neutral like the major grid, but slightly thicker.
    float zAxis = abs(coord.x) / derivative.x;
    float xAxis = abs(coord.y) / derivative.y;
    float axisLine = min(xAxis, zAxis);
    if (drawAxis && axisLine < 1.25) {
        color = vec4(0.3, 0.3, 0.3, 1.0 - min(axisLine / 1.25, 0.75));
    }

    return color;
}

float gridInterval(vec3 gridSize) {
    float extent = max(abs(gridSize.x), abs(gridSize.y));
    if (extent <= 0.0) return 0.1;
    float rawInterval = extent / 10.0;
    float magnitude = pow(10.0, floor(log(rawInterval) / log(10.0)));
    float normalized = rawInterval / magnitude;
    if (normalized <= 1.0) return magnitude;
    if (normalized <= 2.0) return 2.0 * magnitude;
    if (normalized <= 5.0) return 5.0 * magnitude;
    return 10.0 * magnitude;
}

void main() {
    float interval = gridInterval(viewUniforms.gridSize);
    outColor = gridHorizontal(worldPos, 1.0 / interval, true);
}

