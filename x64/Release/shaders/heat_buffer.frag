#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in vec3 fragBaryCoord;
layout(location = 5) in vec2 fragIntrinsicCoord;

// GBuffer outputs
layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gPosition;

// Texture buffers for supporting halfedge data 
layout(set = 0, binding = 1) uniform isamplerBuffer S;  // Supporting halfedge per input triangle
layout(set = 0, binding = 2) uniform samplerBuffer A;   // Supporting angle per input triangle
layout(set = 0, binding = 3) uniform isamplerBuffer H;  // Intrinsic halfedge [origin, edge, face, next]
layout(set = 0, binding = 4) uniform isamplerBuffer E;  // Intrinsic edge [he0, he1]
layout(set = 0, binding = 5) uniform isamplerBuffer T;  // Intrinsic triangle [halfedge]
layout(set = 0, binding = 6) uniform samplerBuffer L;   // Intrinsic edge lengths

// Texture buffers for input mesh data
layout(set = 0, binding = 7) uniform isamplerBuffer H_input;
layout(set = 0, binding = 8) uniform isamplerBuffer E_input;
layout(set = 0, binding = 9) uniform isamplerBuffer T_input;
layout(set = 0, binding = 10) uniform samplerBuffer L_input;

// Heat temperature buffer 
layout(set = 0, binding = 11) uniform samplerBuffer heatColors;  // SurfacePoint data per intrinsic vertex

const float PI = 3.14159265359;
const int IMAX = 200;

vec3 temperatureToColor(float t) {
    // t should be normalized between 0 and 1
    if (t <= 0.0) return vec3(0, 0, 0);                                                          // Black for coldest
    else if (t < 0.25) return mix(vec3(0, 0, 0), vec3(0.1, 0.0, 0.6), t * 4.0);                  // Black to blue
    else if (t < 0.375) return mix(vec3(0.1, 0.0, 0.6), vec3(0.3, 0.0, 0.5), (t - 0.25) * 8.0);  // Blue to violet 
    else if (t < 0.55) return mix(vec3(0.3, 0.0, 0.5), vec3(0.9, 0.0, 0.0), (t - 0.375) * 5.71); // Violet to red 
    else if (t < 0.75) return mix(vec3(0.9, 0.0, 0.0), vec3(0.9, 0.6, 0.0), (t - 0.55) * 5.0);   // Red to orange
    else if (t < 0.9) return mix(vec3(0.9, 0.6, 0.0), vec3(1.0, 1.0, 0.3), (t - 0.75) * 6.67);   // Orange to yellow
    else return mix(vec3(1.0, 1.0, 0.3), vec3(1.0, 1.0, 1.0), (t - 0.9) * 10.0);                 // Yellow to white
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

vec3 bary(vec2 v0, vec2 v1, vec2 v2, vec2 p) {
    float totalArea = area(v0, v1, v2);
    if (abs(totalArea) < 1e-10) {
        return vec3(1.0, 0.0, 0.0);
    }
    float u = area(p, v1, v2) / totalArea;
    float v = area(v0, p, v2) / totalArea;
    float w = 1.0 - u - v;
    return vec3(u, v, w);
}

int mate(int he) {
    if (he < 0) return -1;
    ivec4 halfedge = texelFetch(H, he);
    int edgeIdx = halfedge.g;
    if (edgeIdx < 0) return -1;
    ivec2 edge = texelFetch(E, edgeIdx).rg;
    return (edge.r == he) ? edge.g : edge.r;
}

int findIntrinsicTriangle(vec2 p, out vec3 baryCoords) {
    int inputTri = gl_PrimitiveID;
    
    int h0 = texelFetch(S, inputTri).r;
    float phi0 = texelFetch(A, inputTri).r;
    
    if (h0 < 0) {
        baryCoords = vec3(1, 0, 0);
        return -1;
    }
    
    ivec4 he0 = texelFetch(H, h0);
    int e0 = he0.g;
    if (e0 < 0) {
        baryCoords = vec3(1, 0, 0);
        return -1;
    }
    
    float l0 = texelFetch(L, e0).r;
    
    vec2 v0 = vec2(0, 0);
    vec2 v1 = vec2(l0 * cos(phi0), l0 * sin(phi0));
    
    int lastTriangle = -1;
    int sameTriCount = 0;
    
    for (int iter = 0; iter < IMAX; iter++) {
        ivec4 halfedge = texelFetch(H, h0);
        int t = halfedge.b;
        int h1 = halfedge.a;
        
        if (h1 < 0 || t < 0) {
            baryCoords = vec3(1, 0, 0);
            return -2 - iter;
        }
        
        if (t == lastTriangle) {
            sameTriCount++;
            if (sameTriCount > 3) {
                break;
            }
        } else {
            lastTriangle = t;
            sameTriCount = 0;
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
        
        bool out_v1v2 = !ccw(v1, v2, p);
        bool crosses_v12 = crossing(v1, v2, p);
        
        if (out_v1v2 && crosses_v12) {
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
            // Inside triangle 
            float a = area(v0, v1, v2);
            float a12 = area(v1, v2, p);
            float a20 = area(v2, v0, p);
            float a01 = a - a12 - a20;
            
            int href = texelFetch(T, t).r;
            
            if (href == h0) {
                baryCoords.x = a12 / a;
                baryCoords.y = a20 / a;
            }
            else if (href == h1) {
                baryCoords.x = a20 / a;
                baryCoords.y = a01 / a;
            }
            else {
                baryCoords.x = a01 / a;
                baryCoords.y = a12 / a;
            }
            baryCoords.z = 1.0 - baryCoords.x - baryCoords.y;
            
            return t;
        }
    }
    
    baryCoords = vec3(1, 0, 0);
    return -1;
}

void main() {
    vec3 baryCoords;
    int intrinsicTri = findIntrinsicTriangle(fragIntrinsicCoord, baryCoords);
    
    // Get the intrinsic triangle's vertices
    int href = texelFetch(T, intrinsicTri).r;
    ivec4 he_ref = texelFetch(H, href);
    int h1_ref = he_ref.a;
    ivec4 he1_ref = texelFetch(H, h1_ref);
    int h2_ref = he1_ref.a;
    
    // Get vertex indices
    int v0 = texelFetch(H, href).r;    // origin of href
    int v1 = texelFetch(H, h1_ref).r;  // origin of h1
    int v2 = texelFetch(H, h2_ref).r;  // origin of h2
    
    // Position+temp(texel 0), normal+area(texel 1), color(texel 2)
    // Temperature is in the .w component of texel 0
    float temp0 = texelFetch(heatColors, v0 * 3 + 0).w;
    float temp1 = texelFetch(heatColors, v1 * 3 + 0).w;
    float temp2 = texelFetch(heatColors, v2 * 3 + 0).w;
    
    // Interpolate temperatures using barycentric coordinates
    float interpolatedTemp = baryCoords.x * temp0 + baryCoords.y * temp1 + baryCoords.z * temp2;
    
    // Convert interpolated temperature to color
    float temperatureScale = 50.0;
    float normalized = clamp(interpolatedTemp / temperatureScale, 0.0, 1.0);
    vec3 heatColor = temperatureToColor(normalized);
    
    // Write to GBuffer
    gAlbedo = vec4(heatColor, 1.0);
    gNormal = vec4(normalize(fragNormal), 0.0);
    gPosition = vec4(fragPosition, 1.0);
}
