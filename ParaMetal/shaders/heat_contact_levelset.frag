#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 proj;
    mat4 invView;
} camera;

layout(push_constant) uniform ContactPushConstant {
    vec3 gridMin;
    float cellSize;
    ivec3 gridDim;
    float range;
} pc;

struct ContactRegion {
    vec4 centerExtentW; // xyz = region center, w = extentW (half-depth)
    vec4 normalExtentU; // xyz = region normal, w = extentU (half-width)
    vec4 uAxisExtentV;  // xyz = uAxis,         w = extentV (half-height)
    vec4 vAxis;         // xyz = vAxis,         w = 0.0
    uint channelA;      // SDF channel A index
    uint channelB;      // SDF channel B index
    uint psiChannel;    // Precomputed Psi (sdfA - sdfB) texture channel
    uint pad0;
};

layout(std430, set = 0, binding = 1) readonly buffer RegionBuffer {
    ContactRegion regions[];
};

layout(set = 0, binding = 2) uniform sampler3D sdfTextures[];

layout(set = 0, binding = 3) uniform sampler3D psiTextures[];

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) flat in uint inInstanceIndex;

layout(location = 0) out vec4 outColor;

vec3 worldToUvw(vec3 p) {
    vec3 gridCoord = (p - pc.gridMin) / pc.cellSize;
    return (gridCoord + vec3(0.5)) / vec3(pc.gridDim);
}

float evalSdf(uint channel, vec3 p) {
    return texture(sdfTextures[nonuniformEXT(channel)], worldToUvw(p)).r;
}

float evalPsi(uint psiChannel, vec3 p) {
    return texture(psiTextures[nonuniformEXT(psiChannel)], worldToUvw(p)).r;
}

vec3 gradientSdf(uint channel, vec3 p) {
    float e = 0.5 * pc.cellSize;
    vec3 k1 = vec3( 1.0, -1.0, -1.0);
    vec3 k2 = vec3(-1.0, -1.0,  1.0);
    vec3 k3 = vec3(-1.0,  1.0, -1.0);
    vec3 k4 = vec3( 1.0,  1.0,  1.0);
    return k1 * evalSdf(channel, p + e * k1) +
           k2 * evalSdf(channel, p + e * k2) +
           k3 * evalSdf(channel, p + e * k3) +
           k4 * evalSdf(channel, p + e * k4);
}

float evalCubic(vec4 c, float u) {
    return ((c.x * u + c.y) * u + c.z) * u + c.w;
}

bool bisectCubicZero(vec4 c, float uA, float uB, out float outU) {
    float fA = evalCubic(c, uA);
    float fB = evalCubic(c, uB);
    float tol = 32.0 * 1.1920929e-7 * max(abs(c.x) + abs(c.y) + abs(c.z) + abs(c.w), 1e-20);

    if (abs(fA) <= tol) {
        outU = uA;
        return true;
    }
    if (abs(fB) <= tol) {
        outU = uB;
        return true;
    }
    if (fA * fB > 0.0) {
        return false;
    }

    float lo = uA;
    float hi = uB;
    float fLo = fA;

    for (int i = 0; i < 10; ++i) {
        float mid = 0.5 * (lo + hi);
        float fMid = evalCubic(c, mid);
        if (fLo * fMid <= 0.0) {
            hi = mid;
        } else {
            lo = mid;
            fLo = fMid;
        }
    }

    outU = 0.5 * (lo + hi);
    return true;
}

// Finds earliest root of q(u) = 0 on interval u in [0, 1]
bool solveCellZeroCubic(vec4 c, out float outU) {
    float A = 3.0 * c.x;
    float B = 2.0 * c.y;
    float C = c.z;

    float derivScale = max(max(abs(A), abs(B)), max(abs(C), 1e-20));
    float derivTol = 32.0 * 1.1920929e-7 * derivScale;

    float cp[4];
    int numCp = 0;
    cp[numCp++] = 0.0;

    if (abs(A) > derivTol) {
        float disc = B * B - 4.0 * A * C;
        if (disc >= 0.0) {
            float sqrtDisc = sqrt(disc);
            float u1 = (-B - sqrtDisc) / (2.0 * A);
            float u2 = (-B + sqrtDisc) / (2.0 * A);

            if (u1 > u2) { float tmp = u1; u1 = u2; u2 = tmp; }

            if (u1 > 0.0 && u1 < 1.0) cp[numCp++] = u1;
            if (u2 > 0.0 && u2 < 1.0 && (numCp == 1 || u2 > u1)) cp[numCp++] = u2;
        }
    } else if (abs(B) > derivTol) {
        float u1 = -C / B;
        if (u1 > 0.0 && u1 < 1.0) cp[numCp++] = u1;
    }

    cp[numCp++] = 1.0;

    for (int i = 0; i < numCp - 1; ++i) {
        float rootU = 0.0;
        if (bisectCubicZero(c, cp[i], cp[i + 1], rootU)) {
            outU = rootU;
            return true;
        }
    }

    return false;
}

bool acceptCandidate(
    uint chA,
    uint chB,
    vec3 p,
    out float outSdfA,
    out float outSdfB,
    out vec3 outHitGrad) {
    float sdfValA = evalSdf(chA, p);
    float sdfValB = evalSdf(chB, p);

    // Outside both solids 
    float insideTol = 0.25 * pc.cellSize;
    if (sdfValA < -insideTol || sdfValB < -insideTol) {
        return false;
    }

    // Reject points outside guaranteed valid SDF radius
    float maxValidSdf = 4.0 * pc.cellSize;
    if (sdfValA > maxValidSdf || sdfValB > maxValidSdf) {
        return false;
    }

    // Distance from manifold point to either surface <= range * cellSize
    float surfaceDistance = max(max(sdfValA, 0.0), max(sdfValB, 0.0));
    if (surfaceDistance > pc.range * pc.cellSize) {
        return false;
    }

    // Opposing surface normals
    vec3 gradA = gradientSdf(chA, p);
    vec3 gradB = gradientSdf(chB, p);
    float ga2 = dot(gradA, gradA);
    float gb2 = dot(gradB, gradB);

    if (ga2 <= 1e-12 || gb2 <= 1e-12 || dot(gradA * inversesqrt(ga2), gradB * inversesqrt(gb2)) >= 0.0) {
        return false;
    }

    outSdfA = sdfValA;
    outSdfB = sdfValB;
    outHitGrad = gradA - gradB;
    return true;
}

void main() {
    vec3 cameraPos = camera.invView[3].xyz;
    vec3 rayDir = normalize(fragWorldPos - cameraPos);

    ContactRegion region = regions[inInstanceIndex];
    uint chA = region.channelA;
    uint chB = region.channelB;
    uint psiCh = region.psiChannel;

    vec3 center = region.centerExtentW.xyz;
    float extentW = region.centerExtentW.w;
    vec3 normal = region.normalExtentU.xyz;
    float extentU = region.normalExtentU.w;
    vec3 uAxis = region.uAxisExtentV.xyz;
    float extentV = region.uAxisExtentV.w;
    vec3 vAxis = region.vAxis.xyz;

    float rEffU = extentU + pc.range * pc.cellSize + 0.5 * pc.cellSize;
    float rEffV = extentV + pc.range * pc.cellSize + 0.5 * pc.cellSize;
    float hHalf = extentW + 1.25 * pc.cellSize;
    vec3 boxHalf = vec3(rEffU, rEffV, hHalf);

    // Transform ray into proxy local coordinate space
    vec3 relCam = cameraPos - center;
    vec3 pLocal = vec3(dot(relCam, uAxis), dot(relCam, vAxis), dot(relCam, normal));
    vec3 dLocal = vec3(dot(rayDir, uAxis), dot(rayDir, vAxis), dot(rayDir, normal));

    vec3 safeDLocal = vec3(
        abs(dLocal.x) > 1e-7 ? dLocal.x : (dLocal.x >= 0.0 ? 1e-7 : -1e-7),
        abs(dLocal.y) > 1e-7 ? dLocal.y : (dLocal.y >= 0.0 ? 1e-7 : -1e-7),
        abs(dLocal.z) > 1e-7 ? dLocal.z : (dLocal.z >= 0.0 ? 1e-7 : -1e-7)
    );
    vec3 invDLocal = 1.0 / safeDLocal;

    vec3 t0Local = (-boxHalf - pLocal) * invDLocal;
    vec3 t1Local = ( boxHalf - pLocal) * invDLocal;
    vec3 tMinV = min(t0Local, t1Local);
    vec3 tMaxV = max(t0Local, t1Local);
    float tMin = max(max(tMinV.x, tMinV.y), tMinV.z);
    float tMax = min(min(tMaxV.x, tMaxV.y), tMaxV.z);

    if (tMin > tMax || tMax < 0.0) {
        discard;
    }

    float tEnter = max(tMin, 0.0);
    float tExit = tMax;

    if (tEnter >= tExit) {
        discard;
    }

    vec3 safeRayDir = vec3(
        abs(rayDir.x) > 1e-7 ? rayDir.x : (rayDir.x >= 0.0 ? 1e-7 : -1e-7),
        abs(rayDir.y) > 1e-7 ? rayDir.y : (rayDir.y >= 0.0 ? 1e-7 : -1e-7),
        abs(rayDir.z) > 1e-7 ? rayDir.z : (rayDir.z >= 0.0 ? 1e-7 : -1e-7)
    );
    vec3 invDir = 1.0 / safeRayDir;

    float ddaEps = 1e-5 * pc.cellSize;
    vec3 pStart = cameraPos + rayDir * (tEnter + ddaEps);
    vec3 startCoord = (pStart - pc.gridMin) / pc.cellSize;
    ivec3 cell = clamp(ivec3(floor(startCoord)), ivec3(0), pc.gridDim - ivec3(2));

    ivec3 step = ivec3(
        rayDir.x > 0.0 ? 1 : -1,
        rayDir.y > 0.0 ? 1 : -1,
        rayDir.z > 0.0 ? 1 : -1
    );

    vec3 nextCellPlane = pc.gridMin + (vec3(cell) + max(vec3(step), vec3(0.0))) * pc.cellSize;
    vec3 tMaxAxis = (nextCellPlane - cameraPos) * invDir;
    vec3 tDeltaAxis = abs(vec3(pc.cellSize) * invDir);

    float t = tEnter;
    bool hit = false;
    float tHit = 0.0;
    vec3 hitGrad = vec3(0.0);
    float hitSdfA = 0.0;
    float hitSdfB = 0.0;

    float prevY3 = 0.0;
    bool hasPrevY3 = false;

    float rayLength = tExit - tEnter;
    int maxSteps = min(64, int(ceil(rayLength / pc.cellSize)) * 3 + 3);

    for (int s = 0; s < maxSteps && t < tExit - ddaEps; ++s) {
        float tCellEnd = min(min(tMaxAxis.x, tMaxAxis.y), min(tMaxAxis.z, tExit));
        float span = tCellEnd - t;

        if (span > ddaEps) {
            float y0 = hasPrevY3 ? prevY3 : evalPsi(psiCh, cameraPos + rayDir * t);
            float y1 = evalPsi(psiCh, cameraPos + rayDir * (t + span * (1.0 / 3.0)));
            float y2 = evalPsi(psiCh, cameraPos + rayDir * (t + span * (2.0 / 3.0)));
            float y3 = evalPsi(psiCh, cameraPos + rayDir * tCellEnd);
            prevY3 = y3;
            hasPrevY3 = true;

            float minVal = min(min(y0, y1), min(y2, y3));
            float maxVal = max(max(y0, y1), max(y2, y3));

            // Only solve cubic polynomial if there is a potential sign change in this voxel cell
            if (minVal <= 0.0 && maxVal >= 0.0) {
                float a = 4.5 * (-y0 + 3.0 * y1 - 3.0 * y2 + y3);
                float c = 0.5 * (-11.0 * y0 + 18.0 * y1 - 9.0 * y2 + 2.0 * y3);
                float b = 4.5 * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3);
                float d = y0;
                vec4 cubicCoeffs = vec4(a, b, c, d);

                float rootU = 0.0;
                if (solveCellZeroCubic(cubicCoeffs, rootU)) {
                    float candidateT = t + rootU * span;
                    vec3 pCandidate = cameraPos + rayDir * candidateT;

                    if (acceptCandidate(chA, chB, pCandidate, hitSdfA, hitSdfB, hitGrad)) {
                        tHit = candidateT;
                        hit = true;
                        break;
                    }
                }
            }
        } else {
            hasPrevY3 = false;
        }

        int axis = tMaxAxis.x < tMaxAxis.y
            ? (tMaxAxis.x < tMaxAxis.z ? 0 : 2)
            : (tMaxAxis.y < tMaxAxis.z ? 1 : 2);

        cell[axis] += step[axis];
        tMaxAxis[axis] += tDeltaAxis[axis];
        t = tCellEnd;

        if (cell.x < 0 || cell.x >= pc.gridDim.x - 1 ||
            cell.y < 0 || cell.y >= pc.gridDim.y - 1 ||
            cell.z < 0 || cell.z >= pc.gridDim.z - 1) {
            break;
        }
    }

    if (!hit) {
        discard;
    }

    vec3 pHit = cameraPos + rayDir * tHit;
    vec3 grad = hitGrad;

    float gradSq = dot(grad, grad);
    vec3 surfNormal = gradSq > 1e-12 ? grad * inversesqrt(gradSq) : -rayDir;
    if (dot(surfNormal, -rayDir) < 0.0) {
        surfNormal = -surfNormal;
    }

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.7));
    float diff = max(dot(surfNormal, lightDir), 0.0);
    vec3 halfDir = normalize(lightDir - rayDir);
    float spec = pow(max(dot(surfNormal, halfDir), 0.0), 32.0);

    // Distance from equidistant manifold to either surface in cell units 
    float surfaceDistance = 0.5 * (max(hitSdfA, 0.0) + max(hitSdfB, 0.0));
    float level = surfaceDistance / pc.cellSize;
    float maxRange = max(pc.range, 1.0);
    float fade = clamp(level / maxRange, 0.0, 1.0);

    vec3 coreColor  = vec3(0.78, 0.96, 1.00);
    vec3 midColor   = vec3(0.10, 0.78, 0.95);
    vec3 outerColor = vec3(0.02, 0.28, 0.50);
    vec3 baseColor  = fade < 0.5 ? mix(coreColor, midColor, fade * 2.0) : mix(midColor, outerColor, (fade - 0.5) * 2.0);

    // White contour line at each 1h separation boundary
    float cellFrac = fract(level);
    float edgeDist = min(cellFrac, 1.0 - cellFrac);
    const float lineWidth = 0.025;
    float aa = max(fwidth(edgeDist), 0.002);
    float isoline = 1.0 - smoothstep(lineWidth, lineWidth + aa, edgeDist);
    baseColor = mix(baseColor, vec3(1.0), 0.35 * isoline);

    vec3 litColor = baseColor * (0.35 + 0.55 * diff) + vec3(0.3) * spec;

    vec4 clipPos = camera.proj * camera.view * vec4(pHit, 1.0);
    gl_FragDepth = clipPos.z / clipPos.w;
    outColor = vec4(litColor, 1.0);
}