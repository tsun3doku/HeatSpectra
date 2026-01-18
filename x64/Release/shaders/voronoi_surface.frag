#version 450

layout(location = 0) in flat uint vHintCellID; 
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vModelPos;

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

const uint K_NEIGHBORS = 50;

const uint NEIGHBORS_TO_CHECK = 36;

// Increase iteration for larger triangles
const int MAX_WALK_ITERATIONS = 4; 

const uint SECOND_RING_NEIGHBORS = 2;

const float EMBOSS_EDGE_PIXELS = 2.0;
const float EMBOSS_DARKEN = 0.5;
const float EMBOSS_HIGHLIGHT = 0.25;
const float COLOR_AA_PIXELS = 0.8;
const float EMBOSS_MAX_FRACTION_OF_SEED_DIST = 0.1;
const float COLOR_AA_MAX_FRACTION_OF_SEED_DIST = 0.06;

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

void main() {
    if (vHintCellID >= seeds.length()) {
        outColor = vec4(1, 0, 1, 1); // Error pink
        return;
    }

    vec3 modelSpacePos = vModelPos;

    // Initialize search with the hint from the vertex
    uint bestCellID = vHintCellID;
    vec3 bestSeedPos = seeds[bestCellID].xyz;
    float minDistSq = dot(modelSpacePos - bestSeedPos, modelSpacePos - bestSeedPos);

    // Iterative walk from hint cell to closest cell
    for (int iter = 0; iter < MAX_WALK_ITERATIONS; ++iter) {
        
        bool improved = false;
        uint neighborStart = bestCellID * K_NEIGHBORS;

        // Check all neighbors of the current best cell
        for (uint i = 0; i < NEIGHBORS_TO_CHECK; i++) {
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
    
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(vNormal), lightDir), 0.2);

    float aa = max(fwidth(minBoundaryDist), 1e-8);
    float w = EMBOSS_EDGE_PIXELS * aa;

    float seedDist = sqrt(max(minDistSq, 1e-12));
    float wMax = EMBOSS_MAX_FRACTION_OF_SEED_DIST * seedDist;
    w = min(w, wMax);

    vec3 competitorColor = color;
    if (bestCompetitorID != bestCellID && bestCompetitorID < seeds.length()) {
        competitorColor = paletteColor(bestCompetitorID);
    }

    float colorW = COLOR_AA_PIXELS * aa;
    float colorWMax = COLOR_AA_MAX_FRACTION_OF_SEED_DIST * seedDist;
    colorW = min(colorW, colorWMax);
    float colorBlend = smoothstep(0.0, colorW, minBoundaryDist);
    vec3 blendedCellColor = mix(competitorColor, color, colorBlend);

    float rim = 1.0 - smoothstep(w - aa, w + aa, minBoundaryDist);

    float bandStart = 1.0 * w;
    float bandEnd = 3.5 * w;
    float highlightBand = smoothstep(bandStart - aa, bandStart + aa, minBoundaryDist) *
                          (1.0 - smoothstep(bandEnd - aa, bandEnd + aa, minBoundaryDist));

    vec3 base = blendedCellColor * diff;
    base *= (1.0 - EMBOSS_DARKEN * rim);

    float lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
    vec3 gray = vec3(lum);
    float satStrength = 1.0 + EMBOSS_HIGHLIGHT * highlightBand;
    base = gray + (base - gray) * satStrength;
    base = clamp(base, vec3(0.0), vec3(1.0));

    outColor = vec4(base, 1.0);
}
