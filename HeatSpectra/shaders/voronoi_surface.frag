#version 450

layout(location = 2) in vec3 vModelPos;
layout(location = 3) in vec2 vIntrinsicCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float alpha;
    int _pad0;
    int _pad1;
    int _pad2;
} pc;

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
const float ITER_DIST_SCALE = 0.02; // Tune: higher = more iterations for large triangles
const float EARLY_EXIT_REL_IMPROVEMENT = 1e-4;

const uint SECOND_RING_NEIGHBORS = 2;

const float WIRE_DIST_WIDTH = 0.005;

const float PI = 3.14159265359;
const int IMAX = 1024;

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

bool ccw(vec2 p, vec2 q, vec2 r) {
    float det = (p.x - r.x) * (q.y - r.y) - (p.y - r.y) * (q.x - r.x);
    return det >= 0.0;
}

bool crossing(vec2 v_a, vec2 v_b, vec2 p) {
    vec2 origin = vec2(0.0, 0.0);
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

vec3 clampBarycentric(vec3 b) {
    vec3 clamped = max(b, vec3(0.0));
    float sum = clamped.x + clamped.y + clamped.z;
    if (sum < 1e-8) {
        return vec3(1.0, 0.0, 0.0);
    }
    return clamped / sum;
}

int mate(int he) {
    if (he < 0) return -1;
    ivec4 halfedge = texelFetch(H, he);
    int edgeIdx = halfedge.g;
    if (edgeIdx < 0) return -1;
    ivec2 edge = texelFetch(E, edgeIdx).rg;
    return (edge.r == he) ? edge.g : edge.r;
}

bool loadInputTriangleChart(int inputTri, out ivec3 faceHEs, out vec2 triCoords[3]) {
    int h0 = texelFetch(T_input, inputTri).r;
    if (h0 < 0) {
        return false;
    }

    int h1 = texelFetch(H_input, h0).a;
    if (h1 < 0) {
        return false;
    }

    int h2 = texelFetch(H_input, h1).a;
    if (h2 < 0) {
        return false;
    }

    faceHEs = ivec3(h0, h1, h2);

    int e0 = texelFetch(H_input, h0).g;
    int e1 = texelFetch(H_input, h1).g;
    int e2 = texelFetch(H_input, h2).g;
    if (e0 < 0 || e1 < 0 || e2 < 0) {
        return false;
    }

    float a = texelFetch(L_input, e0).r;
    float b = texelFetch(L_input, e1).r;
    float c = texelFetch(L_input, e2).r;
    if (a <= 0.0 || b <= 0.0 || c <= 0.0) {
        return false;
    }

    triCoords[0] = vec2(0.0, 0.0);
    triCoords[1] = vec2(a, 0.0);
    float x = (a * a + c * c - b * b) / (2.0 * a);
    float y2 = max(c * c - x * x, 0.0);
    triCoords[2] = vec2(x, sqrt(y2));

    return true;
}

bool findIntrinsicTriangleFromSeed(vec2 p, int inputHe, vec2 seedOrigin, float seedPhi, out int intrinsicTri, out int failureCode) {
    int h0 = texelFetch(S, inputHe).r;
    float phi0 = seedPhi + texelFetch(A, inputHe).r;

    if (h0 < 0) {
        intrinsicTri = -1;
        failureCode = -1;
        return false;
    }

    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (e0 < 0) {
        intrinsicTri = -1;
        failureCode = -1;
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
            failureCode = -2 - iter;
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

        float alpha = acos(clamp((l0*l0 + l1*l1 - l2*l2) / (2.0*l0*l1), -1.0, 1.0));
        float phi1 = phi0 + PI - alpha;
        vec2 v2 = vec2(v1.x + l1*cos(phi1), v1.y + l1*sin(phi1));
        float a = area(v0, v1, v2);
        if (abs(a) < 1e-10) {
            break;
        }

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
                float beta = acos(clamp((l0*l0 + l2*l2 - l1*l1) / (2.0*l0*l2), -1.0, 1.0));
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
            failureCode = 0;
            return true;
        }
    }

    intrinsicTri = -1;
    failureCode = -1;
    return false;
}

int findIntrinsicTriangle(int inputTri, vec2 p) {
    ivec3 faceHEs;
    vec2 triCoords[3];
    if (!loadInputTriangleChart(inputTri, faceHEs, triCoords)) {
        return -1;
    }

    int bestFailure = -1;
    bool hadSeed = false;
    for (int i = 0; i < 3; ++i) {
        int inputHe = faceHEs[i];
        int supportHe = texelFetch(S, inputHe).r;
        if (supportHe < 0) {
            continue;
        }

        hadSeed = true;
        vec2 edgeDir = triCoords[(i + 1) % 3] - triCoords[i];
        float seedPhi = atan(edgeDir.y, edgeDir.x);

        int intrinsicTri = -1;
        int failureCode = -1;
        if (findIntrinsicTriangleFromSeed(p, inputHe, triCoords[i], seedPhi, intrinsicTri, failureCode)) {
            return intrinsicTri;
        }

        if (failureCode < bestFailure) {
            bestFailure = failureCode;
        }
    }

    if (!hadSeed) {
        return -1;
    }
    return bestFailure;
}

void main() {
    vec3 modelSpacePos = vModelPos;

    int inputTri = gl_PrimitiveID;
    int intrinsicTri = findIntrinsicTriangle(inputTri, vIntrinsicCoord);
    if (intrinsicTri < 0) {
        outColor = vec4(1, 0, 1, 1);
        return;
    }

    // Initialize search with candidate list
    uint bestCellID = 0xFFFFFFFF;
    float minDistSq = 3.402823466e+38;

    uint candBase = uint(intrinsicTri) * K_CANDIDATES;
    if (candBase + K_CANDIDATES <= candidates.length()) {
        for (uint i = 0; i < K_CANDIDATES; ++i) {
            uint cid = candidates[candBase + i];
            if (cid == 0xFFFFFFFF || cid >= seeds.length()) {
                continue;
            }
            vec3 s = seeds[cid].xyz;
            vec3 d = modelSpacePos - s;
            float distSq = dot(d, d);
            if (distSq < minDistSq) {
                minDistSq = distSq;
                bestCellID = cid;
            }
        }
    }

    if (bestCellID == 0xFFFFFFFF) {
        outColor = vec4(1, 0, 1, 1); // Error pink
        return;
    }

    float hintSeedDist = sqrt(max(minDistSq, 1e-12));
    float pixelScale = length(dFdx(modelSpacePos)) + length(dFdy(modelSpacePos));
    float distScale = hintSeedDist / max(pixelScale, 1e-6);
    float tIter = clamp(distScale * ITER_DIST_SCALE, 0.0, 1.0);
    int maxIters = int(mix(float(MIN_WALK_ITERATIONS), float(MAX_WALK_ITERATIONS), tIter) + 0.5);

    float prevMinDistSq = minDistSq;

    // Iterative walk from hint cell to closest cell
    for (int iter = 0; iter < maxIters; ++iter) {
        
        bool improved = false;
        uint neighborStart = bestCellID * K_NEIGHBORS;

        // Check neighbors of the current best cell 
        uint neighborLimit = (iter == 0) ? NEIGHBORS_TO_CHECK : min(uint(24), NEIGHBORS_TO_CHECK);
        for (uint i = 0; i < neighborLimit; i++) {
            uint neighborIdx = neighborStart + i;
            
            // Bounds check
            if (neighborIdx >= neighborIndices.length()) 
                break;
            
            uint neighborID = neighborIndices[neighborIdx];

            // Skip invalid neighbors
            if (neighborID == 0xFFFFFFFF)
                break;

            if (neighborID >= seeds.length())
                continue;

            vec3 neighborPos = seeds[neighborID].xyz;
            vec3 diff = modelSpacePos - neighborPos;
            float distSq = dot(diff, diff); 

            if (distSq < minDistSq) {
                minDistSq = distSq;
                bestCellID = neighborID;
                improved = true;
            }
        }

        // Local and global minimum found
        if (!improved)
            break;

        float improvement = prevMinDistSq - minDistSq;
        if (improvement <= prevMinDistSq * EARLY_EXIT_REL_IMPROVEMENT) {
            break;
        }
        prevMinDistSq = minDistSq;
    }

    vec3 bestPos = seeds[bestCellID].xyz;

    float minBoundaryDist = 3.402823466e+38;
    uint bestCompetitorID = bestCellID;
    {
        uint neighborStart = bestCellID * K_NEIGHBORS;
        for (uint i = 0; i < NEIGHBORS_TO_CHECK; i++) {
            uint neighborIdx = neighborStart + i;
            if (neighborIdx >= neighborIndices.length())
                break;

            uint neighborID = neighborIndices[neighborIdx];
            if (neighborID == 0xFFFFFFFF)
                break;
            if (neighborID >= seeds.length())
                continue;

            vec3 neighborPos = seeds[neighborID].xyz;
            vec3 d = neighborPos - bestPos;
            float lenD = length(d);
            if (lenD <= 1e-8)
                continue;

            vec3 diffN = modelSpacePos - neighborPos;
            float distSqN = dot(diffN, diffN);
            float boundaryDist = (distSqN - minDistSq) / (2.0 * lenD);
            float absBoundaryDist = abs(boundaryDist);
            if (absBoundaryDist < minBoundaryDist) {
                minBoundaryDist = absBoundaryDist;
                bestCompetitorID = neighborID;
            }

            uint neighborStart2 = neighborID * K_NEIGHBORS;
            for (uint j = 0; j < SECOND_RING_NEIGHBORS; j++) {
                uint neighborIdx2 = neighborStart2 + j;
                if (neighborIdx2 >= neighborIndices.length())
                    break;

                uint neighborID2 = neighborIndices[neighborIdx2];
                if (neighborID2 == 0xFFFFFFFF)
                    break;
                if (neighborID2 >= seeds.length())
                    continue;

                vec3 neighborPos2 = seeds[neighborID2].xyz;
                vec3 d2 = neighborPos2 - bestPos;
                float lenD2 = length(d2);
                if (lenD2 <= 1e-8)
                    continue;

                vec3 diffN2 = modelSpacePos - neighborPos2;
                float distSqN2 = dot(diffN2, diffN2);
                float boundaryDist2 = (distSqN2 - minDistSq) / (2.0 * lenD2);
                float absBoundaryDist2 = abs(boundaryDist2);
                if (absBoundaryDist2 < minBoundaryDist) {
                    minBoundaryDist = absBoundaryDist2;
                    bestCompetitorID = neighborID2;
                }
            }

            if (minBoundaryDist <= 1e-8)
                break;
        }
    }

    vec3 color = paletteColor(bestCellID);
    vec3 wireColor = vec3(0.0, 0.0, 0.0);
    vec3 base = color;

    float invW = 1.0 / max(WIRE_DIST_WIDTH, 1e-6);
    float u = clamp(minBoundaryDist * invW, 0.0, 1.0);
    
    // Calculate the analytical gradient of the distance field.
    // The closest boundary is defined as the bisector plane between bestPos and bestCompetitorPos.
    // The direction of greatest change for minBoundaryDist is exactly the normal to this plane.
    vec3 competitorPos = seeds[bestCompetitorID].xyz;
    vec3 boundaryNormal = normalize(bestPos - competitorPos);
    
    // We project the analytical spatial gradient onto the screen using the rasterizer derivatives.
    // This gives us the exact rate of change of the distance field in screen space pixels,
    // avoiding the spikes that happen when taking dFdx() of non-linear or piecewise functions.
    vec3 dPosDx = dFdx(modelSpacePos);
    vec3 dPosDy = dFdy(modelSpacePos);
    
    float dx = abs(dot(boundaryNormal, dPosDx));
    float dy = abs(dot(boundaryNormal, dPosDy));
    
    vec2 gradX = vec2(dx * invW, 0.0);
    vec2 gradY = vec2(dy * invW, 0.0);
    
    vec4 wcolor = textureGrad(wireframe, vec2(u, 0.5), gradX, gradY);
    base = mix(base, wireColor, wcolor.a);

    outColor = vec4(base, 1.0);
}
