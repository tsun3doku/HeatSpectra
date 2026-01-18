#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in vec3 fragBaryCoord;
layout(location = 5) in vec2 fragIntrinsicCoord;  

layout(location = 0) out vec4 outColor;

// Texture buffers for supporting halfedge data
layout(set = 0, binding = 1) uniform isamplerBuffer S;  // Supporting halfedge per input triangle
layout(set = 0, binding = 2) uniform samplerBuffer A;   // Supporting angle per input triangle
layout(set = 0, binding = 3) uniform isamplerBuffer H;  // Intrinsic halfedge [origin, edge, face, next]
layout(set = 0, binding = 4) uniform isamplerBuffer E;  // Intrinsic edge [he0, he1]
layout(set = 0, binding = 5) uniform isamplerBuffer T;  // Intrinsic triangle [halfedge]
layout(set = 0, binding = 6) uniform samplerBuffer L;   // Intrinsic edge lengths

// Texture buffers for input mesh data
layout(set = 0, binding = 7) uniform isamplerBuffer H_input;    // Input halfedge [origin, edge, face, next]
layout(set = 0, binding = 8) uniform isamplerBuffer E_input;    // Input edge [he0, he1]
layout(set = 0, binding = 9) uniform isamplerBuffer T_input;    // Input triangle [halfedge]
layout(set = 0, binding = 10) uniform samplerBuffer L_input;    // Input edge lengths

const float PI = 3.14159265359;
const int IMAX = 200;  // Maximum iterations for tracing

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

vec3 bary(vec2 v0, vec2 v1, vec2 v2, vec2 p) {
    float totalArea = area(v0, v1, v2);
    if (abs(totalArea) < 1e-10) {
        // Degenerate triangle
        return vec3(1.0, 0.0, 0.0);
    }
    float u = area(p, v1, v2) / totalArea;
    float v = area(v0, p, v2) / totalArea;
    float w = 1.0 - u - v;
    return vec3(u, v, w);
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

// Find which intrinsic triangle contains this fragment
int findIntrinsicTriangle(vec2 p, out vec3 baryCoords) {
    // Get input triangle ID from gl_PrimitiveID 
    int inputTri = gl_PrimitiveID;
    
    // Get supporting info for this input triangle
    int h0 = texelFetch(S, inputTri).r;
    float phi0 = texelFetch(A, inputTri).r;
    
    if (h0 < 0) {
        baryCoords = vec3(1, 0, 0);
        return -1;
    }
    
    // Build local 2D coord system for the starting intrinsic edge
    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (e0 < 0) {
        baryCoords = vec3(1, 0, 0);
        return -1;
    }
    
    float l0 = texelFetch(L, e0).r;
    
    // Always start at origin in this input triangle's local 2D space
    // The angle phi0 orients the supporting halfedge relative to the input triangle's first edge
    vec2 v0 = vec2(0, 0);
    vec2 v1 = vec2(l0 * cos(phi0), l0 * sin(phi0));
    
    // Walk through intrinsic triangulation
    int lastTriangle = -1;
    int sameTriCount = 0;
    int iterCount = 0;
    
    for (int iter = 0; iter < IMAX; iter++) {
        iterCount = iter;
        ivec4 halfedge = texelFetch(H, h0);
        int t = halfedge.b;   // .b = face index
        int h1 = halfedge.a;  // .a = next halfedge
        
        if (h1 < 0 || t < 0) {
            // Hit invalid data 
            baryCoords = vec3(1, 0, 0);
            return -2 - iter;  
        }
        
        // Check if walk is stuck in the same triangle
        if (t == lastTriangle) {
            sameTriCount++;
            if (sameTriCount > 3) {
                // Stuck 
                break;
            }
        } else {
            lastTriangle = t;
            sameTriCount = 0;
        }
        
        ivec4 he1 = texelFetch(H, h1);
        int h2 = he1.a;  // next after h1
        if (h2 < 0) break;
        
        int e1 = he1.g;
        ivec4 he2 = texelFetch(H, h2);
        int e2 = he2.g;
        if (e1 < 0 || e2 < 0) break;
        
        float l1 = texelFetch(L, e1).r;
        float l2 = texelFetch(L, e2).r;
        
        // Compute third vertex using law of cosines
        float alpha = acos(clamp((l0*l0 + l1*l1 - l2*l2) / (2.0*l0*l1), -1.0, 1.0));
        float phi1 = phi0 + PI - alpha;
        vec2 v2 = vec2(v1.x + l1*cos(phi1), v1.y + l1*sin(phi1));
        
        bool out_v1v2 = !ccw(v1, v2, p);
        bool crosses_v12 = crossing(v1, v2, p);
        
        // Check if needs to cross edge v1-v2
        if (out_v1v2 && crosses_v12) {
            int m1 = mate(h1);
            if (m1 != -1) {
                v0 = v2;  // v1 stays same
                l0 = l1;
                h0 = m1;
                phi0 = phi1 + PI;
                continue;
            }
            // Hit boundary
            break;
        }
        // Check if needs to cross edge v2-v0 or v1-v2
        else if (out_v1v2 || !ccw(v2, v0, p)) {
            int m2 = mate(h2);
            if (m2 != -1) {
                float beta = acos(clamp((l0*l0 + l2*l2 - l1*l1) / (2.0*l0*l2), -1.0, 1.0));
                v1 = v2;  // v0 stays same
                l0 = l2;
                h0 = m2;
                phi0 = phi0 + beta;
                continue;
            }
            // Hit boundary
            break;
        }
        // Else, walk is inside the current triangle
        else {
            // Calculate bary coords with respect to the triangle's reference HE
            float a = area(v0, v1, v2);
            float a12 = area(v1, v2, p);
            float a20 = area(v2, v0, p);
            float a01 = a - a12 - a20;
            
            // Get triangle's reference HE to determine vertex order
            int href = texelFetch(T, t).r;
            
            if (href == h0) {
                baryCoords.x = a12 / a;
                baryCoords.y = a20 / a;
            }
            else if (href == h1) {
                baryCoords.x = a20 / a;
                baryCoords.y = a01 / a;
            }
            else { // href == h2
                baryCoords.x = a01 / a;
                baryCoords.y = a12 / a;
            }
            baryCoords.z = 1.0 - baryCoords.x - baryCoords.y;
            
            return t;
        }
    }
    
    // Failed to converge
    baryCoords = vec3(1, 0, 0);
    return -1;
}

void main() {
    // Input triangle index
    int inputTri = gl_PrimitiveID;
    
    // Get supporting HE 
    int h0 = texelFetch(S, inputTri).r;
    if (h0 < 0) {
        // Invalid supporting HE
        outColor = vec4(0, 1, 1, 1);
        return;
    }
    
    // Get angle 
    float phi0 = texelFetch(A, inputTri).r;
    
    vec3 baryCoords;
    // Use the consistent 2D coords from the geometry shader
    int intrinsicTri = findIntrinsicTriangle(fragIntrinsicCoord, baryCoords);
    
    if (intrinsicTri < 0) {
        if (intrinsicTri == -1000) {
            // Point outside input triangle
            outColor = vec4(1, 0, 1, 1);
            return;
        } else if (intrinsicTri < -2) {
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

outColor = vec4(linearColor, 1.0);
}
