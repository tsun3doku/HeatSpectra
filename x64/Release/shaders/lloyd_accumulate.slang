static const uint K_MAX_NEIGHBORS = 50;
static const uint P_MAX = 50;
static const uint V_MAX = 50;
static const uint END_OF_LIST = 255;

static const float CANONICAL_DOMAIN_SIZE = 1000.0f;
static const float VOLUME_EPSILON2 = 1e-30f;

struct MeshTriangle
{
    float4 v0;
    float4 v1;
    float4 v2;
};

struct VoxelGridParams
{
    float3 gridMin;
    float  cellSize;
    int3   gridDim;
    uint   totalCells;
};

struct ConvexCell
{
    uint3 vertices[V_MAX];
    float4 planes[P_MAX];
    uint boundaryNext[P_MAX];
    uint vertexCount;
    uint planeCount;
};

struct LloydParams
{
    uint  nodeCount;
    float alpha;
    float maxStep;
    float pad0;
};

[[vk::binding(0)]] StructuredBuffer<float4> seedPositions;
[[vk::binding(1)]] StructuredBuffer<MeshTriangle>  meshTriangles;
[[vk::binding(2)]] StructuredBuffer<int>           voxelTrianglesList;
[[vk::binding(3)]] StructuredBuffer<int>           voxelOffsets;
[[vk::binding(4)]] StructuredBuffer<uint>          neighborIndices;
[[vk::binding(5)]] StructuredBuffer<uint>          seedFlags;

// xyz = sum(area * centroid), w = sum(area)
[[vk::binding(6)]] RWStructuredBuffer<float4> lloydAccum;

[[vk::binding(7)]] cbuffer VoxelGridParamsBuffer { VoxelGridParams voxelParams; };
[[vk::binding(8)]] cbuffer LloydParamsBuffer { LloydParams params; };

static inline float3 canonicalFromWorld(float3 p)
{
    return (p - voxelParams.gridMin) * voxelParams.cellSize;
}

static inline float3 worldFromCanonical(float3 p)
{
    return p / voxelParams.cellSize + voxelParams.gridMin;
}

static inline float canonicalVoxelSize()
{
    return CANONICAL_DOMAIN_SIZE / float(voxelParams.gridDim.x);
}

static inline int voxelIndex(int3 v)
{
    return v.x + v.y * voxelParams.gridDim.x + v.z * voxelParams.gridDim.x * voxelParams.gridDim.y;
}

static inline float det2x2(float a11, float a12, float a21, float a22)
{
    return a11 * a22 - a12 * a21;
}

static inline double det3x3D(
    double a11, double a12, double a13,
    double a21, double a22, double a23,
    double a31, double a32, double a33)
{
    return a11 * (a22 * a33 - a23 * a32)
         + a12 * (a23 * a31 - a21 * a33)
         + a13 * (a21 * a32 - a22 * a31);
}

static inline double det4x4D(
    double a11, double a12, double a13, double a14,
    double a21, double a22, double a23, double a24,
    double a31, double a32, double a33, double a34,
    double a41, double a42, double a43, double a44)
{
    double m12 = a21 * a12 - a11 * a22;
    double m13 = a31 * a12 - a11 * a32;
    double m14 = a41 * a12 - a11 * a42;
    double m23 = a31 * a22 - a21 * a32;
    double m24 = a41 * a22 - a21 * a42;
    double m34 = a41 * a32 - a31 * a42;

    double m123 = m23 * a13 - m13 * a23 + m12 * a33;
    double m124 = m24 * a13 - m14 * a23 + m12 * a43;
    double m134 = m34 * a13 - m14 * a33 + m13 * a43;
    double m234 = m34 * a23 - m24 * a33 + m23 * a43;

    return (m234 * a14 - m134 * a24 + m124 * a34 - m123 * a44);
}

static inline float det3x3(
    float a11, float a12, float a13,
    float a21, float a22, float a23,
    float a31, float a32, float a33)
{
    return a11 * det2x2(a22, a23, a32, a33)
         - a21 * det2x2(a12, a13, a32, a33)
         + a31 * det2x2(a12, a13, a22, a23);
}

static inline float4 getPlaneFromPoints(float3 A, float3 B, float3 C)
{
    float3 n = cross(B - A, C - A);
    float lenN = length(n);
    if (lenN > 1e-20f) n /= lenN;
    float w = -dot(n, A);
    return float4(n, w);
}

static inline float4 CCPlane(in ConvexCell cell, uint idx)
{
    return cell.planes[idx];
}

static inline void CCSetPlane(inout ConvexCell cell, uint idx, float4 p)
{
    cell.planes[idx] = p;
}

static inline uint CCIthPlane(uint3 v, uint i)
{
    return (i == 0) ? v.x : ((i == 1) ? v.y : v.z);
}

static inline void CCNewVertex(inout ConvexCell cell, uint i, uint j, uint k)
{
    if (cell.vertexCount >= V_MAX) return;
    cell.vertices[cell.vertexCount++] = uint3(i, j, k);
}

static inline float3 computeVertex(uint3 planeTriplet, in ConvexCell cell)
{
    float4 pi1f = CCPlane(cell, planeTriplet.x);
    float4 pi2f = CCPlane(cell, planeTriplet.y);
    float4 pi3f = CCPlane(cell, planeTriplet.z);

    double4 pi1 = double4(pi1f);
    double4 pi2 = double4(pi2f);
    double4 pi3 = double4(pi3f);

    double m12 = pi2.x * pi3.y - pi2.y * pi3.x;
    double m13 = pi2.x * pi3.z - pi2.z * pi3.x;
    double m14 = pi2.x * pi3.w - pi2.w * pi3.x;
    double m23 = pi2.y * pi3.z - pi2.z * pi3.y;
    double m24 = pi2.y * pi3.w - pi2.w * pi3.y;
    double m34 = pi2.z * pi3.w - pi2.w * pi3.z;

    double4 r;
    r.x = -pi1.w * m23 - pi1.y * m34 + pi1.z * m24;
    r.y =  pi1.x * m34 + pi1.w * m13 - pi1.z * m14;
    r.z = -pi1.x * m24 + pi1.y * m14 - pi1.w * m12;
    r.w =  pi1.x * m23 - pi1.y * m13 + pi1.z * m12;

    if (abs(r.w) < 1e-20) return float3(0.0f, 0.0f, 0.0f);
    return float3(float(r.x / r.w), float(r.y / r.w), float(r.z / r.w));
}

static inline bool CCVertexIsInHalfSpace(uint3 vertexPlanes, float4 eqn, in ConvexCell cell)
{
    float4 pi1 = CCPlane(cell, vertexPlanes.x);
    float4 pi2 = CCPlane(cell, vertexPlanes.y);
    float4 pi3 = CCPlane(cell, vertexPlanes.z);

    double det = det4x4D(
        (double)pi1.x, (double)pi2.x, (double)pi3.x, (double)eqn.x,
        (double)pi1.y, (double)pi2.y, (double)pi3.y, (double)eqn.y,
        (double)pi1.z, (double)pi2.z, (double)pi3.z, (double)eqn.z,
        (double)pi1.w, (double)pi2.w, (double)pi3.w, (double)eqn.w);

    return det > 0.0;
}

static inline uint CCComputeBoundary(inout ConvexCell cell, uint nbRemoved)
{
    for (uint i = 0; i < P_MAX; i++) cell.boundaryNext[i] = END_OF_LIST;
    uint firstBoundary = END_OF_LIST;

    uint nbIter = 0;
    uint t = cell.vertexCount;

    while (nbRemoved > 0)
    {
        if (nbIter++ > 1000) return firstBoundary;

        bool isInBorder[3];
        bool nextIsOpp[3];

        for (uint e = 0; e < 3; e++)
            isInBorder[e] = (cell.boundaryNext[CCIthPlane(cell.vertices[t], e)] != END_OF_LIST);

        for (uint e = 0; e < 3; e++)
            nextIsOpp[e] = (cell.boundaryNext[CCIthPlane(cell.vertices[t], (e + 1) % 3)] == CCIthPlane(cell.vertices[t], e));

        bool newBorderIsSimple = true;

        for (uint e = 0; e < 3; e++)
        {
            if (!nextIsOpp[e] && !nextIsOpp[(e + 1) % 3] && isInBorder[(e + 1) % 3])
                newBorderIsSimple = false;
        }

        if (!nextIsOpp[0] && !nextIsOpp[1] && !nextIsOpp[2])
        {
            if (firstBoundary == END_OF_LIST)
            {
                for (uint e = 0; e < 3; e++)
                    cell.boundaryNext[CCIthPlane(cell.vertices[t], e)] = CCIthPlane(cell.vertices[t], (e + 1) % 3);
                firstBoundary = cell.vertices[t].x;
            }
            else
            {
                newBorderIsSimple = false;
            }
        }

        if (!newBorderIsSimple)
        {
            t++;
            if (t == cell.vertexCount + nbRemoved) t = cell.vertexCount;
            continue;
        }

        for (uint e = 0; e < 3; e++)
        {
            if (!nextIsOpp[e])
                cell.boundaryNext[CCIthPlane(cell.vertices[t], e)] = CCIthPlane(cell.vertices[t], (e + 1) % 3);
        }

        for (uint e = 0; e < 3; e++)
        {
            if (nextIsOpp[e] && nextIsOpp[(e + 1) % 3])
            {
                uint p = CCIthPlane(cell.vertices[t], (e + 1) % 3);
                if (firstBoundary == p) firstBoundary = cell.boundaryNext[p];
                cell.boundaryNext[p] = END_OF_LIST;
            }
        }

        uint3 tmp = cell.vertices[t];
        cell.vertices[t] = cell.vertices[cell.vertexCount + nbRemoved - 1];
        cell.vertices[cell.vertexCount + nbRemoved - 1] = tmp;

        t = cell.vertexCount;
        nbRemoved--;
    }

    return firstBoundary;
}

static inline void CCClipByPlane(inout ConvexCell cell, uint newPlaneIdx)
{
    float4 eqn = CCPlane(cell, newPlaneIdx);

    uint nbRemoved = 0;
    uint i = 0;

    while (i < cell.vertexCount)
    {
        if (CCVertexIsInHalfSpace(cell.vertices[i], eqn, cell))
        {
            cell.vertexCount--;
            uint3 tmp = cell.vertices[i];
            cell.vertices[i] = cell.vertices[cell.vertexCount];
            cell.vertices[cell.vertexCount] = tmp;
            nbRemoved++;
        }
        else
        {
            i++;
        }
    }

    if (cell.vertexCount < 1) return;

    if (nbRemoved == 0)
    {
        cell.planeCount--;
        return;
    }

    uint firstBoundary = CCComputeBoundary(cell, nbRemoved);
    if (firstBoundary == END_OF_LIST) return;

    uint cir = firstBoundary;
    uint safety = 0;
    do
    {
        if (safety++ > P_MAX) break;
        uint newCir = cell.boundaryNext[cir];
        CCNewVertex(cell, newPlaneIdx, cir, newCir);
        cir = newCir;
    } while (cir != firstBoundary);
}

static inline void clipPolygonByPlane(inout float3 poly[16], inout uint polyCount, float4 plane)
{
    if (polyCount == 0u) return;

    float3 outPoly[16];
    uint outCount = 0u;

    float3 prev = poly[polyCount - 1u];
    float prevD = dot(plane.xyz, prev) + plane.w;
    bool prevIn = (prevD >= 0.0f);

    for (uint i = 0u; i < polyCount; i++)
    {
        float3 cur = poly[i];
        float curD = dot(plane.xyz, cur) + plane.w;
        bool curIn = (curD >= 0.0f);

        if (curIn != prevIn)
        {
            float denom = (prevD - curD);
            float t = (abs(denom) > 1e-20f) ? (prevD / denom) : 0.0f;
            float3 isect = prev + t * (cur - prev);
            if (outCount < 16u) outPoly[outCount++] = isect;
        }

        if (curIn)
        {
            if (outCount < 16u) outPoly[outCount++] = cur;
        }

        prev = cur;
        prevD = curD;
        prevIn = curIn;
    }

    polyCount = outCount;
    for (uint i = 0u; i < outCount; i++) poly[i] = outPoly[i];
}

static inline void accumulatePolygon(in float3 poly[16], uint polyCount, inout float3 sumPos, inout float sumArea)
{
    if (polyCount < 3u) return;

    float3 v0 = poly[0];
    for (uint i = 1u; i + 1u < polyCount; i++)
    {
        float3 v1 = poly[i];
        float3 v2 = poly[i + 1u];
        float3 c = cross(v1 - v0, v2 - v0);
        float area = 0.5f * length(c);
        if (area <= 1e-20f) continue;

        float3 centroid = (v0 + v1 + v2) * (1.0f / 3.0f);
        sumPos += centroid * area;
        sumArea += area;
    }
}

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint cellID = dtid.x;
    if (cellID >= params.nodeCount) return;

    uint flags = seedFlags[cellID];
    bool isGhost = (flags & 1u) != 0u;

    if (isGhost)
    {
        lloydAccum[cellID] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 seed = seedPositions[cellID].xyz;

    float canonicalVox = canonicalVoxelSize();

    float3 bboxMinW = worldFromCanonical(float3(-0.01f, -0.01f, -0.01f));
    float3 bboxMaxW = worldFromCanonical(float3(CANONICAL_DOMAIN_SIZE + 0.01f, CANONICAL_DOMAIN_SIZE + 0.01f, CANONICAL_DOMAIN_SIZE + 0.01f));

    ConvexCell cell;

    cell.vertexCount = 8;
    cell.vertices[0] = uint3(2, 5, 0);
    cell.vertices[1] = uint3(5, 3, 0);
    cell.vertices[2] = uint3(1, 5, 2);
    cell.vertices[3] = uint3(5, 1, 3);
    cell.vertices[4] = uint3(4, 2, 0);
    cell.vertices[5] = uint3(4, 0, 3);
    cell.vertices[6] = uint3(2, 4, 1);
    cell.vertices[7] = uint3(4, 3, 1);

    CCSetPlane(cell, 0, float4( 1.0f,  0.0f,  0.0f, -bboxMinW.x));
    CCSetPlane(cell, 1, float4(-1.0f,  0.0f,  0.0f,  bboxMaxW.x));
    CCSetPlane(cell, 2, float4( 0.0f,  1.0f,  0.0f, -bboxMinW.y));
    CCSetPlane(cell, 3, float4( 0.0f, -1.0f,  0.0f,  bboxMaxW.y));
    CCSetPlane(cell, 4, float4( 0.0f,  0.0f,  1.0f, -bboxMinW.z));
    CCSetPlane(cell, 5, float4( 0.0f,  0.0f, -1.0f,  bboxMaxW.z));

    cell.planeCount = 6;

    uint baseIdx = cellID * K_MAX_NEIGHBORS;

    for (uint i = 0u; i < K_MAX_NEIGHBORS; i++)
    {
        uint neigh = neighborIndices[baseIdx + i];
        if (neigh == 0xFFFFFFFFu) break;

        float4 B = seedPositions[neigh];
        float3 dir = seed - B.xyz;
        float dirNorm = length(dir);
        if (dirNorm < 1e-10f) continue;

        float3 ave2 = seed + B.xyz;
        float dotProduct = dot(ave2, dir) - (B.w - 1.0f);
        float w = -dotProduct / (2.0f * dirNorm);
        float3 normal = dir / dirNorm;

        if (cell.planeCount >= P_MAX) break;

        uint planeIdx = cell.planeCount;
        CCSetPlane(cell, planeIdx, float4(normal, w));
        cell.planeCount++;

        CCClipByPlane(cell, planeIdx);
        if (cell.vertexCount < 1) break;
    }

    if (cell.vertexCount < 1)
    {
        lloydAccum[cellID] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 bbMin = float3(1e10f, 1e10f, 1e10f);
    float3 bbMax = float3(-1e10f, -1e10f, -1e10f);
    for (uint ve = 0; ve < cell.vertexCount; ve++)
    {
        float3 p = computeVertex(cell.vertices[ve], cell);
        bbMin = min(bbMin, p);
        bbMax = max(bbMax, p);
    }

    float3 bbMinC = canonicalFromWorld(bbMin);
    float3 bbMaxC = canonicalFromWorld(bbMax);

    int3 voxelMin = int3(floor(bbMinC / canonicalVox));
    int3 voxelMax = int3(floor(bbMaxC / canonicalVox));

    voxelMin = max(voxelMin, int3(0, 0, 0));
    voxelMax = min(voxelMax, voxelParams.gridDim - int3(1, 1, 1));

    const int MAX_TRI = 128;
    int uniqueTri[MAX_TRI];
    uint uniqueCount = 0u;

    for (int z = voxelMin.z; z <= voxelMax.z && uniqueCount < MAX_TRI; z++)
    {
        for (int y = voxelMin.y; y <= voxelMax.y && uniqueCount < MAX_TRI; y++)
        {
            for (int x = voxelMin.x; x <= voxelMax.x && uniqueCount < MAX_TRI; x++)
            {
                int vIdx = voxelIndex(int3(x, y, z));
                int minOff = voxelOffsets[vIdx];
                int maxOff = voxelOffsets[vIdx + 1];
                for (int i = minOff; i < maxOff && uniqueCount < MAX_TRI; i++)
                {
                    int triId = voxelTrianglesList[i];
                    bool found = false;
                    for (uint j = 0u; j < uniqueCount; j++) { if (uniqueTri[j] == triId) { found = true; break; } }
                    if (!found) uniqueTri[uniqueCount++] = triId;
                }
            }
        }
    }

    float3 sumPos = float3(0.0f, 0.0f, 0.0f);
    float sumArea = 0.0f;

    for (uint i = 0u; i < uniqueCount; i++)
    {
        int triIdx = uniqueTri[i];
        MeshTriangle tri = meshTriangles[triIdx];

        float3 poly[16];
        uint polyCount = 3u;
        poly[0] = tri.v0.xyz;
        poly[1] = tri.v1.xyz;
        poly[2] = tri.v2.xyz;

        uint planeLimit = min(cell.planeCount, P_MAX);
        for (uint p = 0u; p < planeLimit; p++)
        {
            clipPolygonByPlane(poly, polyCount, CCPlane(cell, p));
            if (polyCount < 3u) break;
        }

        accumulatePolygon(poly, polyCount, sumPos, sumArea);
    }

    lloydAccum[cellID] = float4(sumPos, sumArea);
}
