#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 4) in vec3 fragBaryCoord;

layout(location = 0) out vec4 outColor;

// Texture buffers for supporting halfedge data
layout(set = 0, binding = 1) uniform isamplerBuffer S;  // Supporting halfedge per input halfedge
layout(set = 0, binding = 2) uniform samplerBuffer A;   // Supporting angle per input halfedge
layout(set = 0, binding = 3) uniform isamplerBuffer H;  // Intrinsic halfedge [origin, edge, face, next]
layout(set = 0, binding = 4) uniform isamplerBuffer E;  // Intrinsic edge [he0, he1]
layout(set = 0, binding = 5) uniform isamplerBuffer T;  // Intrinsic triangle [halfedge]
layout(set = 0, binding = 6) uniform samplerBuffer L;   // Intrinsic edge lengths

// Texture buffers for input mesh data
layout(set = 0, binding = 7) uniform isamplerBuffer H_input;    // Input halfedge [origin, edge, face, next]
layout(set = 0, binding = 9) uniform isamplerBuffer T_input;    // Input triangle [halfedge]
layout(set = 0, binding = 10) uniform samplerBuffer L_input;    // Input edge lengths
layout(set = 0, binding = 11) uniform sampler2D wireframe;      // Wireframe texture

const float PI = 3.14159265359;
const int IMAX = 1024;  // Higher budget for thin input triangles over dense intrinsic meshes

bool ccw(vec2 p, vec2 q, vec2 r) {
    float det = (p.x - r.x) * (q.y - r.y) - (p.y - r.y) * (q.x - r.x);
    return det >= 0.0;
}

// Check if ray from origin to p crosses edge v_a to v_b
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

// Get opposite HE
int mate(int he) {
    if (he < 0) 
    return -1;

    ivec4 halfedge = texelFetch(H, he);
    int edgeIdx = halfedge.g;  // .g = edge index

    if (edgeIdx < 0) 
    return -1;

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

vec2 inputChartPoint(vec2 triCoords[3]) {
    return fragBaryCoord.x * triCoords[0] + fragBaryCoord.y * triCoords[1] + fragBaryCoord.z * triCoords[2];
}

bool findIntrinsicTriangleFromSeed(vec2 p, vec2 px, vec2 py, int inputHe, vec2 seedOrigin, float seedPhi, out int intrinsicTri, out vec3 baryCoords, out vec3 duvwdx, out vec3 duvwdy, out int failureCode) {
    int h0 = texelFetch(S, inputHe).r;
    float phi0 = seedPhi + texelFetch(A, inputHe).r;

    if (h0 < 0) {
        baryCoords = vec3(1, 0, 0);
        intrinsicTri = -1;
        failureCode = -1;
        return false;
    }

    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (e0 < 0) {
        baryCoords = vec3(1, 0, 0);
        intrinsicTri = -1;
        failureCode = -1;
        return false;
    }

    float l0 = texelFetch(L, e0).r;

    vec2 v0 = seedOrigin;
    vec2 v1 = v0 + vec2(l0 * cos(phi0), l0 * sin(phi0));

    for (int iter = 0; iter < IMAX; iter++) {
        ivec4 halfedge = texelFetch(H, h0);
        int t = halfedge.b;   // .b = face index
        int h1 = halfedge.a;  // .a = next halfedge

        if (h1 < 0 || t < 0) {
            baryCoords = vec3(1, 0, 0);
            intrinsicTri = -1;
            failureCode = -2 - iter;
            return false;
        }

        ivec4 he1 = texelFetch(H, h1);
        int h2 = he1.a;  // next after h1
        if (h2 < 0) {
            break;
        }

        int e1 = he1.g;
        ivec4 he2 = texelFetch(H, h2);
        int e2 = he2.g;
        if (e1 < 0 || e2 < 0) {
            break;
        }

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
            float a12 = area(v1, v2, p);
            float a20 = area(v2, v0, p);
            float a01 = a - a12 - a20;
            
            float a12x = area(v1, v2, px);
            float a20x = area(v2, v0, px);
            float a01x = a - a12x - a20x;

            float a12y = area(v1, v2, py);
            float a20y = area(v2, v0, py);
            float a01y = a - a12y - a20y;

            vec3 localBary = vec3(a12 / a, a20 / a, a01 / a);
            vec3 localBaryX = vec3(a12x / a, a20x / a, a01x / a);
            vec3 localBaryY = vec3(a12y / a, a20y / a, a01y / a);

            int href = texelFetch(T, t).r;
            vec3 uvw, uvwx, uvwy;
            
            if (href == h0) {
                uvw.x = localBary.x;
                uvw.y = localBary.y;
                uvwx.x = localBaryX.x;
                uvwx.y = localBaryX.y;
                uvwy.x = localBaryY.x;
                uvwy.y = localBaryY.y;
            }
            else if (href == h1) {
                uvw.x = localBary.y;
                uvw.y = localBary.z;
                uvwx.x = localBaryX.y;
                uvwx.y = localBaryX.z;
                uvwy.x = localBaryY.y;
                uvwy.y = localBaryY.z;
            }
            else { // href == h2
                uvw.x = localBary.z;
                uvw.y = localBary.x;
                uvwx.x = localBaryX.z;
                uvwx.y = localBaryX.x;
                uvwy.x = localBaryY.z;
                uvwy.y = localBaryY.x;
            }
            uvw.z = 1.0 - uvw.x - uvw.y;
            uvwx.z = 1.0 - uvwx.x - uvwx.y;
            uvwy.z = 1.0 - uvwy.x - uvwy.y;

            baryCoords = uvw;
            duvwdx = uvwx - uvw;
            duvwdy = uvwy - uvw;

            intrinsicTri = t;
            failureCode = 0;
            return true;
        }
    }

    baryCoords = vec3(1, 0, 0);
    duvwdx = vec3(0, 0, 0);
    duvwdy = vec3(0, 0, 0);
    intrinsicTri = -1;
    failureCode = -1;
    return false;
}

// Find which intrinsic triangle contains this fragment
int findIntrinsicTriangle(ivec3 faceHEs, vec2 triCoords[3], vec2 p, vec2 px, vec2 py, out vec3 baryCoords, out vec3 duvwdx, out vec3 duvwdy) {
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
        vec3 candidateBary = vec3(1, 0, 0);
        vec3 candidateDuvwdx = vec3(0, 0, 0);
        vec3 candidateDuvwdy = vec3(0, 0, 0);
        if (findIntrinsicTriangleFromSeed(p, px, py, inputHe, triCoords[i], seedPhi, intrinsicTri, candidateBary, candidateDuvwdx, candidateDuvwdy, failureCode)) {
            baryCoords = candidateBary;
            duvwdx = candidateDuvwdx;
            duvwdy = candidateDuvwdy;
            return intrinsicTri;
        }

        if (failureCode < bestFailure) {
            bestFailure = failureCode;
        }
    }

    baryCoords = vec3(1, 0, 0);
    duvwdx = vec3(0, 0, 0);
    duvwdy = vec3(0, 0, 0);
    if (!hadSeed) {
        return -1;
    }
    return bestFailure;
}

void main() {
    int inputTri = gl_PrimitiveID;

    ivec3 faceHEs;
    vec2 triCoords[3];
    if (!loadInputTriangleChart(inputTri, faceHEs, triCoords)) {
        outColor = vec4(0, 1, 1, 1);
        return;
    }

    vec3 baryCoords, duvwdx, duvwdy;
    vec2 p = inputChartPoint(triCoords);
    vec2 px = p + dFdx(p);
    vec2 py = p + dFdy(p);
    int intrinsicTri = findIntrinsicTriangle(faceHEs, triCoords, p, px, py, baryCoords, duvwdx, duvwdy);
    
    if (intrinsicTri < 0) {
        if (intrinsicTri < -2) {
            // Hit invalid data during walk
            int iter = -(intrinsicTri + 2);
            float intensity = float(iter) / 50.0;
            outColor = vec4(0, 0, intensity, 1);
            return;
        } else {
            // Walk completed but didnt converge
            outColor = vec4(0.8, 0.8, 0.8, 1);
            return;
        }
    }
    
// Randomize index 
const float goldenRatio = 0.618033988749895;
float randomVal = fract(float(intrinsicTri) * goldenRatio);
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

// Intrinsic wireframe using intrinsic barycentric derivatives
    vec3 color = linearColor;
    vec3 wireColor = vec3(0.0, 0.0, 0.0);
    vec3 fx = duvwdx;
    vec3 fy = duvwdy;

    vec4 wcolor = textureGrad(wireframe, vec2(baryCoords.x, 0.5), vec2(fx.x, 0.0), vec2(fy.x, 0.0));
    color = mix(color, wireColor, wcolor.a);

    wcolor = textureGrad(wireframe, vec2(baryCoords.y, 0.5), vec2(fx.y, 0.0), vec2(fy.y, 0.0));
    color = mix(color, wireColor, wcolor.a);

    wcolor = textureGrad(wireframe, vec2(baryCoords.z, 0.5), vec2(fx.z, 0.0), vec2(fy.z, 0.0));
    color = mix(color, wireColor, wcolor.a);

    color = clamp(color, vec3(0.0), vec3(1.0));

outColor = vec4(color, 1.0);
}
