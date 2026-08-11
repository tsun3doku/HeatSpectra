#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in vec3 fragBaryCoord;
layout(location = 5) in vec2 fragIntrinsicCoord;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    vec4 sourceParams; // x = temperature, y = isSource
    uint palette;
    float minTemperature;
    float maxTemperature;
} push;

// GBuffer outputs
layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gMaterial;

// Texture buffers for supporting halfedge data 
layout(set = 0, binding = 1) uniform isamplerBuffer S;  // Supporting halfedge per input halfedge
layout(set = 0, binding = 2) uniform samplerBuffer A;   // Supporting angle per input halfedge
layout(set = 0, binding = 3) uniform isamplerBuffer H;  // Intrinsic halfedge [origin, edge, face, next]
layout(set = 0, binding = 4) uniform isamplerBuffer E;  // Intrinsic edge [he0, he1]
layout(set = 0, binding = 5) uniform isamplerBuffer T;  // Intrinsic triangle [halfedge]
layout(set = 0, binding = 6) uniform samplerBuffer L;   // Intrinsic edge lengths

// Texture buffers for input mesh data
layout(set = 0, binding = 7) uniform isamplerBuffer H_input;
layout(set = 0, binding = 8) uniform isamplerBuffer E_input;
layout(set = 0, binding = 9) uniform isamplerBuffer T_input;
layout(set = 0, binding = 10) uniform samplerBuffer L_input;

struct SurfacePoint {
    vec3 position;
    float temperatureC;
    vec3 normal;
    float vertexArea;
    vec4 color;
};

// Heat temperature buffer 
layout(set = 0, binding = 11) readonly buffer HeatColors {
    SurfacePoint surfacePoints[];
} heatColors;

layout(set = 0, binding = 12) uniform sampler2D infernoLut;
layout(set = 0, binding = 13) uniform sampler2D viridisLut;
layout(set = 0, binding = 14) uniform sampler2D inferno2Lut;
layout(set = 0, binding = 15) uniform sampler2D parulaLut;

const float PI = 3.14159265359;
const float WALK_RELATIVE_AREA_EPSILON = 1e-8;
const int WALK_MAX_STEPS = 256;
const int WALK_SUCCESS = 0;
const int WALK_NO_SEED = -1;
const int WALK_INVALID_TOPOLOGY = -2;
const int WALK_DEGENERATE = -3;
const int WALK_BOUNDARY = -4;
const int WALK_LIMIT_REACHED = -5;
const int WALK_STALLED = -6;
const vec3 errorColor = vec3(1.0, 0.0, 1.0);
const float HEAT_ROUGHNESS = 0.05;
const float HEAT_METALNESS = 0.0;
const float HEAT_LIGHTING_MIX = 0.1;
const float HEAT_CONTOUR_SPACING = 20.0;

vec4 heatMaterial() {
    return vec4(HEAT_ROUGHNESS, HEAT_METALNESS, HEAT_LIGHTING_MIX, 0.0);
}

vec3 srgbToLinear(vec3 c) {
    vec3 clamped = clamp(c, vec3(0.0), vec3(1.0));
    vec3 low = clamped / 12.92;
    vec3 high = pow((clamped + 0.055) / 1.055, vec3(2.4));
    bvec3 useLow = lessThanEqual(clamped, vec3(0.04045));
    return vec3(
        useLow.x ? low.x : high.x,
        useLow.y ? low.y : high.y,
        useLow.z ? low.z : high.z);
}

void writeHeatSurface(vec3 albedo) {
    gAlbedo = vec4(albedo, 1.0);
    gMaterial = heatMaterial();
}

vec3 samplePaletteLut(sampler2D lut, float normalized) {
    const float paletteSteps = 32.0;
    float scaled = clamp(normalized, 0.0, 1.0) * (paletteSteps - 1.0);
    float lowerStep = floor(scaled);
    float upperStep = min(lowerStep + 1.0, paletteSteps - 1.0);
    float interpolation = fract(scaled);

    ivec2 lutSize = textureSize(lut, 0);
    int sampleX = lutSize.x / 2;
    int lowerY = int(round((lowerStep / (paletteSteps - 1.0)) * float(lutSize.y - 1)));
    int upperY = int(round((upperStep / (paletteSteps - 1.0)) * float(lutSize.y - 1)));

    vec3 lowerSrgb = texelFetch(lut, ivec2(sampleX, lowerY), 0).rgb;
    vec3 upperSrgb = texelFetch(lut, ivec2(sampleX, upperY), 0).rgb;
    return srgbToLinear(mix(lowerSrgb, upperSrgb, interpolation));
}

vec3 heatColorFromTemperature(float temperature) {
    float temperatureRange = max(push.maxTemperature - push.minTemperature, 1e-6);
    float normalizedTemperature = (temperature - push.minTemperature) / temperatureRange;
    float paletteCoordinate = 1.0 - normalizedTemperature;
    if (push.palette == 0u) {
        return samplePaletteLut(inferno2Lut, paletteCoordinate);
    }
    if (push.palette == 1u) {
        return samplePaletteLut(parulaLut, paletteCoordinate);
    }
    if (push.palette == 2u) {
        return samplePaletteLut(viridisLut, paletteCoordinate);
    }
    return samplePaletteLut(infernoLut, paletteCoordinate);
}

float heatContourMask(float temperature) {
    const float flatFieldTolerance = 1e-4;
    float pixelGradient = fwidth(temperature);
    if (pixelGradient <= flatFieldTolerance) {
        return 1.0;
    }

    float contourPhase = temperature / HEAT_CONTOUR_SPACING;
    float distanceToContour = abs(fract(contourPhase + 0.5) - 0.5) * HEAT_CONTOUR_SPACING;
    float contourHalfWidth = max(pixelGradient * 1.5, flatFieldTolerance);
    return smoothstep(0.0, contourHalfWidth, distanceToContour);
}

vec3 heatAlbedo(float temperature) {
    vec3 heatColor = heatColorFromTemperature(temperature);
    return mix(vec3(0.0), heatColor, heatContourMask(temperature));
}

struct WalkResult {
    int triangle;
    int status;
    int steps;
    vec3 bary;
    vec3 baryDx;
    vec3 baryDy;
};

WalkResult makeWalkResult(int status) {
    WalkResult result;
    result.triangle = -1;
    result.status = status;
    result.steps = 0;
    result.bary = vec3(1.0, 0.0, 0.0);
    result.baryDx = result.bary;
    result.baryDy = result.bary;
    return result;
}

bool validIndex(int index, int count) {
    return index >= 0 && index < count;
}

bool validLength(float lengthValue) {
    return lengthValue > 0.0 && !isnan(lengthValue) && !isinf(lengthValue);
}

float triangleAreaEpsilon(float a, float b, float c) {
    float longestEdge = max(a, max(b, c));
    return longestEdge * longestEdge * WALK_RELATIVE_AREA_EPSILON;
}

bool ccw(vec2 p, vec2 q, vec2 r) {
    float det = (p.x - r.x) * (q.y - r.y) - (p.y - r.y) * (q.x - r.x);
    return det >= 0.0;
}

bool crossing(vec2 origin, vec2 v_a, vec2 v_b, vec2 p) {
    bool t0 = ccw(origin, p, v_a);
    bool t1 = ccw(origin, p, v_b);
    bool u0 = ccw(v_a, v_b, origin);
    bool u1 = ccw(v_a, v_b, p);
    return ((t0 && !t1) || (!t0 && t1)) && ((u0 && !u1) || (!u0 && u1));
}

float area(vec2 p, vec2 q, vec2 r) {
    float det = (q.x * r.y + p.x * q.y + p.y * r.x) - (q.x * p.y + r.x * q.y + r.y * p.x);
    return det / 2.0;
}

int mate(int he, int halfedgeCount, int edgeCount) {
    if (!validIndex(he, halfedgeCount)) return -2;

    int edgeIdx = texelFetch(H, he).g;
    if (!validIndex(edgeIdx, edgeCount)) return -2;

    ivec2 edge = texelFetch(E, edgeIdx).rg;
    int opposite = -2;
    if (edge.r == he) opposite = edge.g;
    else if (edge.g == he) opposite = edge.r;

    if (opposite == -1) return -1;
    if (!validIndex(opposite, halfedgeCount) || opposite == he) return -2;
    return opposite;
}

bool loadInputTriangleChart(int inputTri, out ivec3 faceHEs, out vec2 triCoords[3]) {
    int inputTriangleCount = textureSize(T_input);
    int inputHalfedgeCount = textureSize(H_input);
    if (!validIndex(inputTri, inputTriangleCount)) return false;

    int h0 = texelFetch(T_input, inputTri).r;
    if (!validIndex(h0, inputHalfedgeCount)) return false;

    int h1 = texelFetch(H_input, h0).a;
    if (!validIndex(h1, inputHalfedgeCount)) return false;

    int h2 = texelFetch(H_input, h1).a;
    if (!validIndex(h2, inputHalfedgeCount) || texelFetch(H_input, h2).a != h0) return false;

    faceHEs = ivec3(h0, h1, h2);

    int e0 = texelFetch(H_input, h0).g;
    int e1 = texelFetch(H_input, h1).g;
    int e2 = texelFetch(H_input, h2).g;
    int inputEdgeCount = textureSize(L_input);
    if (!validIndex(e0, inputEdgeCount) || !validIndex(e1, inputEdgeCount) || !validIndex(e2, inputEdgeCount)) return false;

    float a = texelFetch(L_input, e0).r;
    float b = texelFetch(L_input, e1).r;
    float c = texelFetch(L_input, e2).r;
    if (!validLength(a) || !validLength(b) || !validLength(c)) return false;

    triCoords[0] = vec2(0.0, 0.0);
    triCoords[1] = vec2(a, 0.0);
    float x = (a * a + c * c - b * b) / (2.0 * a);
    float y2 = c * c - x * x;
    if (!(y2 > triangleAreaEpsilon(a, b, c)) || isnan(y2) || isinf(y2)) return false;
    triCoords[2] = vec2(x, sqrt(y2));
    return true;
}

vec2 inputChartPoint(vec2 triCoords[3], vec3 inputBary) {
    return inputBary.x * triCoords[0] + inputBary.y * triCoords[1] + inputBary.z * triCoords[2];
}

bool remapBarycentric(int href, int h0, int h1, int h2, vec3 localBary, out vec3 bary) {
    if (href == h0) bary = localBary;
    else if (href == h1) bary = localBary.yzx;
    else if (href == h2) bary = localBary.zxy;
    else return false;
    return true;
}

WalkResult walkFromSeed(vec2 p, vec2 px, vec2 py, int inputHe, vec2 seedOrigin, float seedPhi, bool calculateDerivatives) {
    WalkResult result = makeWalkResult(WALK_INVALID_TOPOLOGY);
    int supportCount = textureSize(S);
    int angleCount = textureSize(A);
    int halfedgeCount = textureSize(H);
    int edgeCount = textureSize(E);
    int triangleCount = textureSize(T);
    int lengthCount = textureSize(L);
    if (!validIndex(inputHe, supportCount) || !validIndex(inputHe, angleCount)) return result;

    int h0 = texelFetch(S, inputHe).r;
    if (h0 < 0) {
        result.status = WALK_NO_SEED;
        return result;
    }
    if (!validIndex(h0, halfedgeCount)) return result;

    float phi0 = seedPhi + texelFetch(A, inputHe).r;
    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (!validIndex(e0, lengthCount)) return result;

    float l0 = texelFetch(L, e0).r;
    if (!validLength(l0) || isnan(phi0) || isinf(phi0)) return result;

    vec2 v0 = seedOrigin;
    vec2 v1 = v0 + l0 * vec2(cos(phi0), sin(phi0));
    for (int iter = 0; iter < WALK_MAX_STEPS; ++iter) {
        result.steps = iter + 1;
        if (!validIndex(h0, halfedgeCount)) return result;

        ivec4 halfedge = texelFetch(H, h0);
        int t = halfedge.b;
        int h1 = halfedge.a;
        if (!validIndex(t, triangleCount) || !validIndex(h1, halfedgeCount)) return result;

        ivec4 he1 = texelFetch(H, h1);
        int h2 = he1.a;
        if (!validIndex(h2, halfedgeCount)) return result;

        ivec4 he2 = texelFetch(H, h2);
        if (he2.a != h0) return result;

        int e1 = he1.g;
        int e2 = he2.g;
        if (!validIndex(e1, lengthCount) || !validIndex(e2, lengthCount)) return result;

        float l1 = texelFetch(L, e1).r;
        float l2 = texelFetch(L, e2).r;
        if (!validLength(l1) || !validLength(l2)) return result;

        float alphaDenominator = 2.0 * l0 * l1;
        if (!validLength(alphaDenominator)) return result;
        float alpha = acos(clamp((l0 * l0 + l1 * l1 - l2 * l2) / alphaDenominator, -1.0, 1.0));
        float phi1 = phi0 + PI - alpha;
        vec2 v2 = v1 + l1 * vec2(cos(phi1), sin(phi1));
        float triangleArea = area(v0, v1, v2);
        if (isnan(triangleArea) || isinf(triangleArea) ||
            abs(triangleArea) <= triangleAreaEpsilon(l0, l1, l2)) {
            result.status = WALK_DEGENERATE;
            return result;
        }

        bool outsideEdge12 = !ccw(v1, v2, p);
        bool outsideEdge20 = !ccw(v2, v0, p);
        if (outsideEdge12 && (ccw(v2, v0, p) || crossing(seedOrigin, v1, v2, p))) {
            int nextH = mate(h1, halfedgeCount, edgeCount);
            if (nextH == -1) {
                result.status = WALK_BOUNDARY;
                return result;
            }
            if (nextH < 0) return result;

            v0 = v2;
            l0 = l1;
            h0 = nextH;
            phi0 = phi1 + PI;
            continue;
        }

        if (outsideEdge20) {
            int nextH = mate(h2, halfedgeCount, edgeCount);
            if (nextH == -1) {
                result.status = WALK_BOUNDARY;
                return result;
            }
            if (nextH < 0) return result;

            float betaDenominator = 2.0 * l0 * l2;
            if (!validLength(betaDenominator)) return result;
            float beta = acos(clamp((l0 * l0 + l2 * l2 - l1 * l1) / betaDenominator, -1.0, 1.0));

            v1 = v2;
            l0 = l2;
            h0 = nextH;
            phi0 += beta;
            continue;
        }

        vec3 localBary = vec3(
            area(v1, v2, p) / triangleArea,
            area(v2, v0, p) / triangleArea,
            0.0);
        localBary.z = 1.0 - localBary.x - localBary.y;

        int href = texelFetch(T, t).r;
        if (!remapBarycentric(href, h0, h1, h2, localBary, result.bary)) return result;

        if (calculateDerivatives) {
            vec3 localBaryX = vec3(
                area(v1, v2, px) / triangleArea,
                area(v2, v0, px) / triangleArea,
                0.0);
            localBaryX.z = 1.0 - localBaryX.x - localBaryX.y;

            vec3 localBaryY = vec3(
                area(v1, v2, py) / triangleArea,
                area(v2, v0, py) / triangleArea,
                0.0);
            localBaryY.z = 1.0 - localBaryY.x - localBaryY.y;

            if (!remapBarycentric(href, h0, h1, h2, localBaryX, result.baryDx) ||
                !remapBarycentric(href, h0, h1, h2, localBaryY, result.baryDy)) return result;
        } else {
            result.baryDx = result.bary;
            result.baryDy = result.bary;
        }

        result.triangle = t;
        result.status = WALK_SUCCESS;
        return result;
    }

    result.status = WALK_LIMIT_REACHED;
    return result;
}

WalkResult locateIntrinsicTriangle(ivec3 faceHEs, vec2 triCoords[3], vec2 p, vec2 px, vec2 py, bool calculateDerivatives) {
    int inputHe = faceHEs[0];
    if (!validIndex(inputHe, textureSize(S)) || texelFetch(S, inputHe).r < 0) {
        return makeWalkResult(WALK_NO_SEED);
    }

    vec2 edgeDir = triCoords[1] - triCoords[0];
    return walkFromSeed(p, px, py, inputHe, triCoords[0], atan(edgeDir.y, edgeDir.x), calculateDerivatives);
}

void main() {
    if (push.sourceParams.y > 0.5) {
        writeHeatSurface(heatColorFromTemperature(push.sourceParams.x));
        return;
    }

    int inputTri = gl_PrimitiveID;
    ivec3 faceHEs;
    vec2 triCoords[3];
    if (!loadInputTriangleChart(inputTri, faceHEs, triCoords)) {
        writeHeatSurface(errorColor);
        return;
    }

    vec2 p = inputChartPoint(triCoords, fragBaryCoord);
    WalkResult walk = locateIntrinsicTriangle(faceHEs, triCoords, p, p, p, false);
    if (walk.status != WALK_SUCCESS) {
        writeHeatSurface(errorColor);
        return;
    }

    int href = texelFetch(T, walk.triangle).r;
    if (href < 0) {
        writeHeatSurface(errorColor);
        return;
    }

    ivec4 he_ref = texelFetch(H, href);
    int h1_ref = he_ref.a;
    ivec4 he1_ref = texelFetch(H, h1_ref);
    int h2_ref = he1_ref.a;
    
    int v0 = he_ref.r;
    int v1 = he1_ref.r;
    int v2 = texelFetch(H, h2_ref).r;

    if (v0 < 0 || v1 < 0 || v2 < 0) {
        writeHeatSurface(errorColor);
        return;
    }
    
    float temp0 = heatColors.surfacePoints[v0].temperatureC;
    float temp1 = heatColors.surfacePoints[v1].temperatureC;
    float temp2 = heatColors.surfacePoints[v2].temperatureC;

    float interpolatedTemp = walk.bary.x * temp0 + walk.bary.y * temp1 + walk.bary.z * temp2;
    writeHeatSurface(heatAlbedo(interpolatedTemp));
}
