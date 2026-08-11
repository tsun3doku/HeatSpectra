#version 450

layout(location = 2) in vec3 vModelPosition;
layout(location = 3) in vec2 vIntrinsicCoord;
layout(location = 5) in vec3 vWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outMaterial;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float alpha;
    float canonicalToWorldScale;
    float _pad0;
    float _pad1;
} pc;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(std430, binding = 2) readonly buffer SeedBuffer {
    vec4 seeds[]; // xyz, w=padding
};

layout(std430, binding = 4) readonly buffer NeighborBuffer {
    uint neighborIndices[];
};

// Intrinsic walk buffers
layout(set = 0, binding = 6) uniform isamplerBuffer S;  // Supporting halfedge per input halfedge
layout(set = 0, binding = 7) uniform samplerBuffer A;   // Supporting angle per input halfedge
layout(set = 0, binding = 8) uniform isamplerBuffer H;  // Intrinsic halfedge [origin, edge, face, next]
layout(set = 0, binding = 9) uniform isamplerBuffer E;  // Intrinsic edge [he0, he1]
layout(set = 0, binding = 10) uniform isamplerBuffer T; // Intrinsic triangle [halfedge]
layout(set = 0, binding = 11) uniform samplerBuffer L;  // Intrinsic edge lengths

layout(set = 0, binding = 12) uniform isamplerBuffer H_input;    // Input halfedge [origin, edge, face, next]
layout(set = 0, binding = 13) uniform isamplerBuffer E_input;    // Input edge [he0, he1]
layout(set = 0, binding = 14) uniform isamplerBuffer T_input;    // Input triangle [halfedge]
layout(set = 0, binding = 15) uniform samplerBuffer L_input;     // Input edge lengths

layout(std430, binding = 16) readonly buffer CandidateBuffer {
    uint candidates[];
};

layout(set = 0, binding = 17) uniform sampler2D wireframe;       // Wireframe texture

const uint K_NEIGHBORS = 50;
const uint K_CANDIDATES = 64;

const uint NEIGHBORS_TO_CHECK = 36;

// Adaptive walk budget
const int MIN_WALK_ITERATIONS = 4;
const int MAX_WALK_ITERATIONS = 16;
const float ITER_DIST_SCALE = 0.02; // Higher = more iterations for large triangles
const float EARLY_EXIT_REL_IMPROVEMENT = 1e-4;

const uint SECOND_RING_NEIGHBORS = 2;

const float WIRE_DIST_WIDTH = 0.005;
const float EXIT_PLANE_DEPTH_SCALE = 0.90;
const float EXIT_PLANE_MIN_DEPTH = 0.08;
const float INTERIOR_EDGE_WIDTH = 0.035;
const float INTERIOR_EDGE_STRENGTH = 0.6;
const float VORONOI_ROUGHNESS = 0.05;
const float VORONOI_METALNESS = 0.0;
const float VORONOI_LIGHTING_MIX = 0.5;
const float PI = 3.14159265359;
const int IMAX = 128;

vec4 voronoiMaterial() {
    return vec4(VORONOI_ROUGHNESS, VORONOI_METALNESS, VORONOI_LIGHTING_MIX, 0.0);
}

void writeVoronoiSurface(vec3 albedo) {
    outColor = vec4(albedo, 1.0);
    outMaterial = voronoiMaterial();
}

vec3 applyWireMask(vec3 baseColor, float wireMask) {
    return mix(baseColor, vec3(0.0), wireMask);
}

vec3 paletteColor(uint cellID) {
    // Randomize index using golden ratio for even distribution
    const float goldenRatio = 0.618033988749895;
    float randomVal = fract(float(cellID) * goldenRatio);
    int index = int(randomVal * 7.0); 
    
    vec3 palette[7] = vec3[](
        vec3(0.510, 0.765, 0.941), // #82c3f0 (Denim Blue)
        vec3(0.812, 0.922, 1.000), // #cfebff (Pale Blue Lily)
        vec3(0.976, 0.922, 0.651), // #f9eba6 (Light Tan)
        vec3(1.000, 0.820, 0.871), // #ffd1de (Light Pink)
        vec3(1.000, 0.600, 0.750), // #ff99bf (Pastel Magenta)
        vec3(0.647, 0.949, 0.776), // #a5f2c6 (Mint Green)
        vec3(0.780, 0.640, 0.940)  // #c7a3f0 (Lilac)
    );
    
    vec3 sRGB = palette[index];
    vec3 linearColor = pow(sRGB, vec3(2.2));
    
    float saturation = 1.2; 
    float luminance = dot(linearColor, vec3(0.2126, 0.7152, 0.0722));
    linearColor = mix(vec3(luminance), linearColor, saturation);
    
    return linearColor;
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    if (len2 < 1e-12) {
        return fallback;
    }
    return v * inversesqrt(len2);
}

vec3 modelSpaceViewRay(vec3 worldPosition, mat3 invModelMatrix) {
    vec3 cameraPos = inverse(ubo.view)[3].xyz;
    vec3 viewRayWorld = worldPosition - cameraPos;
    return normalize(invModelMatrix * viewRayWorld);
}

bool seedBisectorPlane(vec3 cellPos, uint neighborID, out vec3 planeNormal, out vec3 midpoint) {
    if (neighborID >= seeds.length()) return false;
    vec3 d = cellPos - seeds[neighborID].xyz;
    planeNormal = safeNormalize(d, vec3(0.0));
    midpoint = (cellPos + seeds[neighborID].xyz) * 0.5;
    return dot(planeNormal, planeNormal) > 0.0;
}

vec3 applyExitPlaneShading(vec3 baseColor, float exitDepth, vec3 exitPlaneNormal, float seedDistanceHint, mat3 normalMatrix, vec3 viewRayModel) {
    float depth01 = clamp(exitDepth / max(seedDistanceHint * EXIT_PLANE_DEPTH_SCALE, 1e-6), 0.0, 1.0);
    if (exitDepth >= 1e30 || depth01 < EXIT_PLANE_MIN_DEPTH) {
        return baseColor;
    }

    vec3 wallNormal = safeNormalize(normalMatrix * exitPlaneNormal, vec3(0.0, 0.0, 1.0));
    float viewFacing = abs(dot(wallNormal, safeNormalize(-viewRayModel, vec3(0.0, 0.0, 1.0))));
    float wallLight = mix(0.62, 0.94, viewFacing);
    float luminance = dot(baseColor, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), baseColor, 1.10) * wallLight;
}

float interiorWallEdgeMask(uint cellID, uint exitNeighborID, vec3 cellPos, vec3 exitPlaneNormal, vec3 hitPos, vec3 dHitPosDx, vec3 dHitPosDy) {
    if (exitNeighborID == 0xFFFFFFFF) return 0.0;

    float edgeMask = 0.0;
    vec3 exitNormal = safeNormalize(exitPlaneNormal, vec3(0.0, 0.0, 1.0));
    float exitD = dot(exitNormal, hitPos);
    uint neighborStart = cellID * K_NEIGHBORS;

    for (uint i = 0; i < NEIGHBORS_TO_CHECK; ++i) {
        uint neighborIdx = neighborStart + i;
        if (neighborIdx >= neighborIndices.length()) break;

        uint neighborID = neighborIndices[neighborIdx];
        if (neighborID == 0xFFFFFFFF) break;
        if (neighborID == exitNeighborID) continue;

        vec3 planeNormal, midpoint;
        if (!seedBisectorPlane(cellPos, neighborID, planeNormal, midpoint)) continue;

        vec3 lineDir = cross(exitNormal, planeNormal);
        float denom = dot(lineDir, lineDir);
        if (denom < 1e-6) continue;

        float planeD = dot(planeNormal, midpoint);
        vec3 linePoint = (cross(planeNormal, lineDir) * exitD + cross(lineDir, exitNormal) * planeD) / denom;
        vec3 lineNormalOnWall = safeNormalize(cross(lineDir, exitNormal), planeNormal);
        float lineDist = abs(dot(hitPos - linePoint, lineNormalOnWall));

        float invInteriorWidth = 1.0 / max(INTERIOR_EDGE_WIDTH * pc.canonicalToWorldScale, 1e-6);
        float dx = abs(dot(dHitPosDx, lineNormalOnWall));
        float dy = abs(dot(dHitPosDy, lineNormalOnWall));
        vec2 texDx = vec2(dx * invInteriorWidth, 0.0);
        vec2 texDy = vec2(dy * invInteriorWidth, 0.0);
        vec4 lineTex = textureGrad(
            wireframe,
            vec2(clamp(lineDist * invInteriorWidth, 0.0, 1.0), 0.5),
            texDx,
            texDy);
        edgeMask = max(edgeMask, lineTex.a);
    }
    return edgeMask;
}

bool ccw(vec2 p, vec2 q, vec2 r) {
    return ((p.x - r.x) * (q.y - r.y) - (p.y - r.y) * (q.x - r.x)) >= 0.0;
}

bool crossing(vec2 v_a, vec2 v_b, vec2 p) {
    // Check if the segment v_a v_b is crossed by the ray to p
    vec2 origin = vec2(0.0);
    return (ccw(origin, p, v_a) != ccw(origin, p, v_b)) && (ccw(v_a, v_b, origin) != ccw(v_a, v_b, p));
}

float area(vec2 p, vec2 q, vec2 r) {
    // 2D cross product determinant for signed area of triangle (p, q, r)
    float det = (q.x * r.y + p.x * q.y + p.y * r.x) - (q.x * p.y + r.x * q.y + r.y * p.x);
    return det / 2.0;
}

int mate(int he) {
    if (he < 0) return -1;
    int edgeIdx = texelFetch(H, he).g;
    if (edgeIdx < 0) return -1;
    ivec2 edge = texelFetch(E, edgeIdx).rg;
    return (edge.r == he) ? edge.g : edge.r;
}

bool loadInputTriangleChart(int inputTri, out ivec3 faceHEs, out vec2 triCoords[3]) {
    // Fetch input halfedges for the triangle face
    int h0 = texelFetch(T_input, inputTri).r;
    int h1 = (h0 >= 0) ? texelFetch(H_input, h0).a : -1;
    int h2 = (h1 >= 0) ? texelFetch(H_input, h1).a : -1;
    if (h2 < 0) return false;

    faceHEs = ivec3(h0, h1, h2);

    // Fetch input edge indices
    int e0 = texelFetch(H_input, h0).g;
    int e1 = texelFetch(H_input, h1).g;
    int e2 = texelFetch(H_input, h2).g;
    if (e0 < 0 || e1 < 0 || e2 < 0) return false;

    // Fetch edge lengths and verify positivity
    float a = texelFetch(L_input, e0).r;
    float b = texelFetch(L_input, e1).r;
    float c = texelFetch(L_input, e2).r;
    if (a <= 0.0 || b <= 0.0 || c <= 0.0) return false;

    // Construct local 2D coordinate system (flatten triangle to 2D chart)
    triCoords[0] = vec2(0.0);
    triCoords[1] = vec2(a, 0.0);
    float x = (a * a + c * c - b * b) / (2.0 * a);
    triCoords[2] = vec2(x, sqrt(max(c * c - x * x, 0.0)));
    return true;
}

bool findIntrinsicTriangleFromSeed(vec2 p, int inputHe, vec2 seedOrigin, float seedPhi, out int intrinsicTri) {
    int h0 = texelFetch(S, inputHe).r;
    float phi0 = seedPhi + texelFetch(A, inputHe).r;
    if (h0 < 0) {
        intrinsicTri = -1;
        return false;
    }

    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (e0 < 0) {
        intrinsicTri = -1;
        return false;
    }

    float l0 = texelFetch(L, e0).r;
    vec2 v0 = seedOrigin;
    vec2 v1 = v0 + vec2(l0 * cos(phi0), l0 * sin(phi0));

    for (int iter = 0; iter < IMAX; iter++) {
        ivec4 halfedge = texelFetch(H, h0);
        int t = halfedge.b;
        int h1 = halfedge.a;
        if (h1 < 0 || t < 0) {
            intrinsicTri = -1;
            return false;
        }

        ivec4 he1 = texelFetch(H, h1);
        int h2 = he1.a;
        if (h2 < 0) break;

        int e1 = he1.g;
        ivec4 he2 = texelFetch(H, h2);
        int e2 = he2.g;
        if (e1 < 0 || e2 < 0) break;

        float l1 = texelFetch(L, e1).r;
        float l2 = texelFetch(L, e2).r;

        float denom1 = 2.0 * l0 * l1;
        float alpha = (denom1 > 1e-9) ? acos(clamp((l0*l0 + l1*l1 - l2*l2) / denom1, -1.0, 1.0)) : 0.0;
        float phi1 = phi0 + PI - alpha;
        vec2 v2 = vec2(v1.x + l1*cos(phi1), v1.y + l1*sin(phi1));
        if (abs(area(v0, v1, v2)) < 1e-10) break;

        bool out_v1v2 = !ccw(v1, v2, p);
        if (out_v1v2 && crossing(v1, v2, p)) {
            int m1 = mate(h1);
            if (m1 != -1) {
                v0 = v2;
                l0 = l1;
                h0 = m1;
                phi0 = phi1 + PI;
                continue;
            }
            break;
        }
        else if (out_v1v2 || !ccw(v2, v0, p)) {
            int m2 = mate(h2);
            if (m2 != -1) {
                float denom2 = 2.0 * l0 * l2;
                float beta = (denom2 > 1e-9) ? acos(clamp((l0*l0 + l2*l2 - l1*l1) / denom2, -1.0, 1.0)) : 0.0;
                v1 = v2;
                l0 = l2;
                h0 = m2;
                phi0 = phi0 + beta;
                continue;
            }
            break;
        }
        else {
            intrinsicTri = t;
            return true;
        }
    }

    intrinsicTri = -1;
    return false;
}

int findIntrinsicTriangle(int inputTri, vec2 p) {
    ivec3 faceHEs;
    vec2 triCoords[3];
    if (!loadInputTriangleChart(inputTri, faceHEs, triCoords)) return -1;

    for (int i = 0; i < 3; ++i) {
        int inputHe = faceHEs[i];
        if (texelFetch(S, inputHe).r < 0) continue;

        vec2 edgeDir = triCoords[(i + 1) % 3] - triCoords[i];
        int intrinsicTri = -1;
        if (findIntrinsicTriangleFromSeed(p, inputHe, triCoords[i], atan(edgeDir.y, edgeDir.x), intrinsicTri)) {
            return intrinsicTri;
        }
    }
    return -1;
}

void main() {
    vec3 modelSpacePos = vModelPosition;
    int inputTri = gl_PrimitiveID;
    int intrinsicTri = findIntrinsicTriangle(inputTri, vIntrinsicCoord);
    if (intrinsicTri < 0) {
        writeVoronoiSurface(vec3(1, 0, 1));
        return;
    }

    uint bestCellID = 0xFFFFFFFF;
    float minDistSq = 3.402823466e+38;

    uint candBase = uint(intrinsicTri) * K_CANDIDATES;
    uint limit = min(candidates.length() - candBase, K_CANDIDATES);
    for (uint i = 0; i < limit; ++i) {
        uint cid = candidates[candBase + i];
        if (cid != 0xFFFFFFFF && cid < seeds.length()) {
            vec3 d = modelSpacePos - seeds[cid].xyz;
            float distSq = dot(d, d);
            if (distSq < minDistSq) {
                minDistSq = distSq;
                bestCellID = cid;
            }
        }
    }

    if (bestCellID == 0xFFFFFFFF) {
        writeVoronoiSurface(vec3(1, 0, 1));
        return;
    }

    float hintSeedDist = sqrt(max(minDistSq, 1e-12));
    float pixelScale = length(dFdx(modelSpacePos)) + length(dFdy(modelSpacePos));
    float distScale = hintSeedDist / max(pixelScale, 1e-6);
    float tIter = clamp(distScale * ITER_DIST_SCALE, 0.0, 1.0);
    int maxIters = int(mix(float(MIN_WALK_ITERATIONS), float(MAX_WALK_ITERATIONS), tIter) + 0.5);

    float prevMinDistSq = minDistSq;

    for (int iter = 0; iter < maxIters; ++iter) {
        bool improved = false;
        uint neighborStart = bestCellID * K_NEIGHBORS;
        uint neighborLimit = (iter == 0) ? NEIGHBORS_TO_CHECK : min(uint(24), NEIGHBORS_TO_CHECK);
        for (uint i = 0; i < neighborLimit; i++) {
            uint neighborIdx = neighborStart + i;
            if (neighborIdx >= neighborIndices.length()) break;
            
            uint neighborID = neighborIndices[neighborIdx];
            if (neighborID == 0xFFFFFFFF) break;
            if (neighborID >= seeds.length()) continue;

            vec3 diff = modelSpacePos - seeds[neighborID].xyz;
            float distSq = dot(diff, diff); 
            if (distSq < minDistSq) {
                minDistSq = distSq;
                bestCellID = neighborID;
                improved = true;
            }
        }
        if (!improved || (prevMinDistSq - minDistSq) <= prevMinDistSq * EARLY_EXIT_REL_IMPROVEMENT) break;
        prevMinDistSq = minDistSq;
    }

    vec3 bestPos = seeds[bestCellID].xyz;
    vec3 dPosDx = dFdx(modelSpacePos);
    vec3 dPosDy = dFdy(modelSpacePos);
    mat3 invModelMatrix = inverse(mat3(pc.modelMatrix));
    mat3 normalMatrix = transpose(invModelMatrix);
    vec3 viewRayModel = modelSpaceViewRay(vWorldPosition, invModelMatrix);

    float minBoundaryDist = 3.402823466e+38;
    float exitPlaneDepth = 3.402823466e+38;
    vec3 exitPlaneNormal = vec3(0.0);
    uint exitPlaneNeighborID = 0xFFFFFFFF;
    uint bestCompetitorID = bestCellID;

    uint neighborStart = bestCellID * K_NEIGHBORS;
    for (uint i = 0; i < NEIGHBORS_TO_CHECK; i++) {
        uint neighborIdx = neighborStart + i;
        if (neighborIdx >= neighborIndices.length()) break;

        uint neighborID = neighborIndices[neighborIdx];
        if (neighborID == 0xFFFFFFFF) break;

        vec3 planeNormal, midpoint;
        if (!seedBisectorPlane(bestPos, neighborID, planeNormal, midpoint)) continue;

        float boundaryDist = dot(planeNormal, modelSpacePos - midpoint);
        float rayRate = dot(planeNormal, viewRayModel);
        if (rayRate < -1e-5) {
            float exitT = max(boundaryDist, 0.0) / -rayRate;
            if (exitT < exitPlaneDepth) {
                exitPlaneDepth = exitT;
                exitPlaneNormal = planeNormal;
                exitPlaneNeighborID = neighborID;
            }
        }

        float absBoundaryDist = abs(boundaryDist);
        if (absBoundaryDist < minBoundaryDist) {
            minBoundaryDist = absBoundaryDist;
            bestCompetitorID = neighborID;
        }

        uint neighborStart2 = neighborID * K_NEIGHBORS;
        for (uint j = 0; j < SECOND_RING_NEIGHBORS; j++) {
            uint neighborIdx2 = neighborStart2 + j;
            if (neighborIdx2 >= neighborIndices.length()) break;

            uint neighborID2 = neighborIndices[neighborIdx2];
            if (neighborID2 == 0xFFFFFFFF) break;
            
            vec3 planeNormal2, midpoint2;
            if (seedBisectorPlane(bestPos, neighborID2, planeNormal2, midpoint2)) {
                float absBoundaryDist2 = abs(dot(planeNormal2, modelSpacePos - midpoint2));
                if (absBoundaryDist2 < minBoundaryDist) {
                    minBoundaryDist = absBoundaryDist2;
                    bestCompetitorID = neighborID2;
                }
            }
        }
    }

    vec3 cellColor = paletteColor(bestCellID);
    vec3 depthShadedColor = applyExitPlaneShading(cellColor, exitPlaneDepth, exitPlaneNormal, hintSeedDist, normalMatrix, viewRayModel);

    float invW = 1.0 / max(WIRE_DIST_WIDTH * pc.canonicalToWorldScale, 1e-6);
    vec3 competitorPos = seeds[bestCompetitorID].xyz;
    vec3 boundaryNormal = safeNormalize(bestPos - competitorPos, vec3(0.0, 0.0, 1.0));

    // Screen space derivatives of boundary distance for wireframe filter footprint
    float dx = abs(dot(boundaryNormal, dPosDx));
    float dy = abs(dot(boundaryNormal, dPosDy));
    vec2 gradX = vec2(dx * invW, 0.0);
    vec2 gradY = vec2(dy * invW, 0.0);

    float u = clamp(minBoundaryDist * invW, 0.0, 1.0);
    vec4 boundaryWire = textureGrad(wireframe, vec2(min(u, 0.5), 0.5), gradX, gradY);
    
    if (exitPlaneDepth < 1e30) {
        vec3 hitPos = modelSpacePos + viewRayModel * exitPlaneDepth;
        // Screen space derivatives of the hit position for wall edge filtering
        vec3 dHitPosDx = dFdx(hitPos);
        vec3 dHitPosDy = dFdy(hitPos);
        float wallEdge = interiorWallEdgeMask(bestCellID, exitPlaneNeighborID, bestPos, exitPlaneNormal, hitPos, dHitPosDx, dHitPosDy);
        wallEdge *= 1.0 - boundaryWire.a;
        depthShadedColor = mix(depthShadedColor, depthShadedColor * 0.28, wallEdge * INTERIOR_EDGE_STRENGTH);
    }

    vec3 albedo = applyWireMask(depthShadedColor, boundaryWire.a);
    writeVoronoiSurface(albedo);
}
