#version 450

layout(location = 0) in flat int planeID;
layout(location = 1) in vec3 worldPos;
layout(location = 2) in vec3 cameraPos;
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



// Draw wall grid
vec4 gridWall(vec3 fragPos3D, float scale, int plane) {
    // Calculate wall normal pointing outward
    vec3 wallNormal;
    if (plane == 1) wallNormal = vec3(1.0, 0.0, 0.0);       // +X wall
    else if (plane == 2) wallNormal = vec3(-1.0, 0.0, 0.0); // -X wall
    else if (plane == 3) wallNormal = vec3(0.0, 0.0, 1.0);  // +Z wall
    else wallNormal = vec3(0.0, 0.0, -1.0);                 // -Z wall
    
    // Direction from grid center to camera
    vec3 centerToCamera = normalize(cameraPos);
    
    float dotProduct = dot(wallNormal, centerToCamera);
    
    float wallOpacity = 1.0;
    if (dotProduct > 0.0) {
        // Smooth fade based on camera direction
        wallOpacity = 1.0 - smoothstep(0.0, 0.25, dotProduct);
    }
    
    // Draw grid based on plane orientation
    vec2 coord;
    if (plane == 1 || plane == 2) {
        // YZ walls
        coord = fragPos3D.zy * scale;
    }
    else {
        // XY walls
        coord = fragPos3D.xy * scale;
    }
    
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    
    // Add greater lines 
    float greaterLineMargin = 0.15;
    float greaterScale = 0.2;
    
    float greaterLineX = abs(fract(coord.x * greaterScale + 0.5) - 0.5) / derivative.x;
    float greaterLineY = abs(fract(coord.y * greaterScale + 0.5) - 0.5) / derivative.y;
    
    vec4 color = vec4(0.05, 0.05, 0.05, smoothstep(0.0, 1.5, (1.0 - min(line, 1.0))));
    
    // Make greater lines brighter
    if (greaterLineX < greaterLineMargin || greaterLineY < greaterLineMargin) {
        color = vec4(0.3, 0.3, 0.3, (1.0 - min(line, 0.6)));
    }
    
    color.a *= 0.6 * wallOpacity;
    return color;
}
void main() {
    vec4 gridColor;
    
    if (planeID == 0) {
        // Floor plane
        gridColor = gridHorizontal(worldPos, 10, true);
    }
    else {
        // Wall planes
        gridColor = gridWall(worldPos, 10, planeID);
    }
    
    outColor = gridColor;
}


