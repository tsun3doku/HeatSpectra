#version 450

layout(location = 1) in vec3 nearPoint;
layout(location = 2) in vec3 farPoint;
layout(location = 3) in mat4 fragView;
layout(location = 7) in mat4 fragProj;
layout(location = 11) in vec3 cameraPos;
layout(location = 0) out vec4 outColor;

vec4 grid(vec3 fragPos3D, float scale, bool drawAxis) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y) * 2.0; //line scale
    
    float minimumZ = min(derivative.y, 1.0);
    float minimumX = min(derivative.x, 1.0);
   
    float greaterLineMargin = 0.15;
    float greaterScaleX = 0.25; //scale factor for the X-axis line density
    float greaterScaleZ = 0.25; //scale factor for the Z-axis line density

    float greaterLineX = abs(fract(coord.y * greaterScaleX + 0.5) - 0.5) / derivative.y;
    float greaterLineZ = abs(fract(coord.x * greaterScaleZ + 0.5) - 0.5) / derivative.x ;

    float axisMargin = .08;

    float maxRadius = 10; //radius
    float fallOff = 25; //falloff radius
    float distanceFromCenter = length(vec2(fragPos3D.x, fragPos3D.z)); //calculate falloff from center

    float gradientOpacity = smoothstep(maxRadius, maxRadius - fallOff, distanceFromCenter);

    vec4 color = vec4(0.025, 0.025, 0.025, smoothstep(0.0, 1.5, (1.0 - min(line, 1.0))) * gradientOpacity);

    if (greaterLineX < greaterLineMargin) {
        color = vec4(0.2, 0.2, 0.2, (1.0 - min(line,0.6)) * gradientOpacity); 
    }

    if (greaterLineZ < greaterLineMargin) {
        color = vec4(0.2, 0.2, 0.2, (1.0 - min(line,0.6)) * gradientOpacity); 
    }
    //z-axis
    if(fragPos3D.x > -axisMargin * minimumX && fragPos3D.x < axisMargin * minimumX) {
        color = vec4(0.0, 0.05, 0.9, (1.0 - min(line,0.6)) * gradientOpacity); //change min when changing axismargin
    }
    //x-axis
    if(fragPos3D.z > -axisMargin * minimumZ && fragPos3D.z < axisMargin * minimumZ) {
        color = vec4(0.9, 0.0, 0.05, (1.0 - min(line,0.6)) * gradientOpacity); //change min when changing axismargin
    }

    return color;
}
float computeDepth(vec3 pos) {
    vec4 clipSpacePos = fragProj * fragView * vec4(pos.xyz, 1.0);
    return (clipSpacePos.z / clipSpacePos.w);
}
void main() {
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);

    vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    vec3 viewDirection = normalize(fragPos3D - cameraPos);

    vec3 camForward = normalize(-cameraPos);
    float angle = acos(clamp(dot(camForward, vec3(0.0, -1.0, 0.0)), -1.0, 1.0));
    float fadeFactor = 1 - smoothstep(1.2, 1.57, abs(angle));

    gl_FragDepth = computeDepth(fragPos3D);
    vec4 gridColor = grid(fragPos3D, 10, true);
    gridColor.a *= fadeFactor;
    outColor = gridColor * float(t > 0);
}


