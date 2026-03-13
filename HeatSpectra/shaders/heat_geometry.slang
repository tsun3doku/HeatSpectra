static const uint K_MAX_NEIGHBORS = 50;
static const uint P_MAX = 50;
static const uint V_MAX = 50;
static const uint END_OF_LIST = 255;

static const uint DEBUG_MAX_PLANE_AREAS = 50;
static const uint DEBUG_DUMP_CELL_COUNT = 8;

static const float CANONICAL_DOMAIN_SIZE = 1000.0f;
static const float VOLUME_EPSILON2 = 1e-30f;

struct VoronoiNode
{
    float temperature;
    float conductivityPerMass;
    float thermalMass;
    float density;
    float specificHeat;
    float conductivity;
    float volume;
    uint  neighborOffset;
    uint  neighborCount;
    uint  interfaceNeighborCount;
};

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

struct DebugPlaneArea
{
    uint   planeIndex;
    uint   neighborCellID;
    float  area;
    float  _padding;
};

struct VoronoiDumpInfo
{
    uint   cellID;
    uint   planeAreaCount;
    uint   _padding0;
    uint   _padding2;

    float4 seedPos;              

    float  unrestrictedVolume;
    float  restrictedVolume;
    float  totalMeshVolume;
    uint   negativeVolumeCellCount;
    float  negativeVolumeSumAbs;
    float2 _padding1;

    DebugPlaneArea planeAreas[DEBUG_MAX_PLANE_AREAS];
};

struct DebugCellGeometry
{
    uint   cellID;
    uint   vertexCount;
    uint   triangleCount;
    float  volume;
    float4 vertices[48];
    uint4  triangles[96];
};

static const uint DEBUG_CELL_IDS[DEBUG_DUMP_CELL_COUNT] = { 59903, 0, 0, 0, 0, 0, 0, 0 };

[[vk::binding(0)]]  RWStructuredBuffer<VoronoiNode> nodes;
[[vk::binding(1)]]  StructuredBuffer<MeshTriangle>  meshTriangles;
[[vk::binding(2)]]  StructuredBuffer<float4>        seedPositions;

[[vk::binding(5)]] cbuffer VoxelGridParamsBuffer { VoxelGridParams voxelParams; };

[[vk::binding(6)]] StructuredBuffer<uint>          occupancy;
[[vk::binding(9)]] StructuredBuffer<int>           voxelTrianglesList;
[[vk::binding(10)]] StructuredBuffer<int>          voxelOffsets;

[[vk::binding(11)]] StructuredBuffer<uint>          neighborIndices;
[[vk::binding(12)]] RWStructuredBuffer<float>       interfaceAreas;
[[vk::binding(13)]] RWStructuredBuffer<uint>        interfaceNeighborIds;

[[vk::binding(14)]] RWStructuredBuffer<DebugCellGeometry> debugCells;

[[vk::binding(15)]] StructuredBuffer<uint>          seedFlags;
[[vk::binding(16)]] RWStructuredBuffer<VoronoiDumpInfo> dumpInfo;

struct GeometryDebugPushConstants
{
    uint debugEnable;
    uint nodeOffset;
    uint nodeCount;
    uint _padding;
};

[[vk::push_constant]] cbuffer GeometryDebugPushConstantsBuffer
{
    GeometryDebugPushConstants pushConstants;
};

static inline float det2x2(float a11, float a12, float a21, float a22)
{
    return a11 * a22 - a12 * a21;
}

static inline void exportUnrestrictedCell(uint cellID, in ConvexCell cell, float volume)
{
    DebugCellGeometry outCell;
    outCell.cellID = cellID;
    outCell.volume = volume;

    uint vCount = min(cell.vertexCount, 48u);
    outCell.vertexCount = vCount;

    for (uint i = 0u; i < 48u; i++)
    {
        outCell.vertices[i] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    for (uint i = 0u; i < 96u; i++)
    {
        outCell.triangles[i] = uint4(0u, 0u, 0u, 0u);
    }

    for (uint i = 0u; i < vCount; i++)
    {
        float3 pos = computeVertex(cell.vertices[i], cell);
        outCell.vertices[i] = float4(pos, 1.0f);
    }

    uint totalTriangles = 0u;
    uint planeLimit = min(cell.planeCount, 48u);

    for (uint planeIdx = 0u; planeIdx < planeLimit && totalTriangles < 96u; planeIdx++)
    {
        float3 facePos[48];
        uint faceIdx[48];
        uint faceCount = 0u;

        for (uint v = 0u; v < vCount && faceCount < 48u; v++)
        {
            uint3 triplet = cell.vertices[v];
            if (triplet.x == planeIdx || triplet.y == planeIdx || triplet.z == planeIdx)
            {
                facePos[faceCount] = computeVertex(triplet, cell);
                faceIdx[faceCount] = v;
                faceCount++;
            }
        }

        if (faceCount < 3u)
            continue;

        float3 faceCenter = float3(0.0f, 0.0f, 0.0f);
        for (uint i = 0u; i < faceCount; i++)
        {
            faceCenter += facePos[i];
        }
        faceCenter *= (1.0f / (float)faceCount);

        float3 normal = CCPlane(cell, planeIdx).xyz;
        float n2 = dot(normal, normal);
        if (n2 < 1e-20f)
            continue;
        normal *= rsqrt(n2);

        float3 refAxis = (abs(normal.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 U = cross(refAxis, normal);
        float u2 = dot(U, U);
        if (u2 < 1e-20f)
            continue;
        U *= rsqrt(u2);
        float3 V = cross(normal, U);

        // Sort by angle around faceCenter using bubble sort
        for (uint i = 0u; i + 1u < faceCount; i++)
        {
            for (uint j = i + 1u; j < faceCount; j++)
            {
                float3 di = facePos[i] - faceCenter;
                float3 dj = facePos[j] - faceCenter;

                float angleI = atan2(dot(di, V), dot(di, U));
                float angleJ = atan2(dot(dj, V), dot(dj, U));

                if (angleJ < angleI)
                {
                    float3 tp = facePos[i];
                    facePos[i] = facePos[j];
                    facePos[j] = tp;

                    uint ti = faceIdx[i];
                    faceIdx[i] = faceIdx[j];
                    faceIdx[j] = ti;
                }
            }
        }

        // Triangulate face with a fan
        for (uint i = 1u; i + 1u < faceCount && totalTriangles < 96u; i++)
        {
            outCell.triangles[totalTriangles++] = uint4(faceIdx[0], faceIdx[i], faceIdx[i + 1u], 0u);
        }
    }

    outCell.triangleCount = totalTriangles;
    debugCells[cellID] = outCell;
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

static inline float4 getPlaneFromPoints(float3 A, float3 B, float3 C)
{
    float3 n = cross(B - A, C - A);
    float lenN = length(n);
    if (lenN > 1e-20f) n /= lenN;
    float w = -dot(n, A);
    return float4(n, w);
}

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

static inline int cornerIndex(int3 c)
{
    int stride = voxelParams.gridDim.x + 1;
    return c.z * stride * stride + c.y * stride + c.x;
}

static inline int voxelIndex(int3 v)
{
    return v.x + v.y * voxelParams.gridDim.x + v.z * voxelParams.gridDim.x * voxelParams.gridDim.y;
}

static inline int getDebugSlot(uint cellID)
{
    for (int i = 0; i < (int)DEBUG_DUMP_CELL_COUNT; i++)
    {
        if (DEBUG_CELL_IDS[i] == cellID) return i;
    }
    return -1;
}

struct ConvexCell
{
    uint3 vertices[V_MAX];
    float4 planes[P_MAX];
    uint boundaryNext[P_MAX];
    uint vertexCount;
    uint planeCount;
};

struct ConvexCellBackup
{
    uint3 vertices[V_MAX];
    uint vertexCount;
    uint planeCount;
};

static inline void CCBUInit(out ConvexCellBackup backup, in ConvexCell cell)
{
    backup.vertexCount = cell.vertexCount;
    backup.planeCount = cell.planeCount;
    for (uint i = 0; i < cell.vertexCount; i++) backup.vertices[i] = cell.vertices[i];
}

static inline void CCBURestore(in ConvexCellBackup backup, inout ConvexCell cell)
{
    cell.vertexCount = backup.vertexCount;
    cell.planeCount = backup.planeCount;
    for (uint i = 0; i < backup.vertexCount; i++) cell.vertices[i] = backup.vertices[i];
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

static inline bool CCIsSecurityRadiusReached(in ConvexCell cell, float4 lastNeig, float3 seed)
{
    float vDist = 0.0f;
    for (uint i = 0; i < cell.vertexCount; i++)
    {
        float3 pc = computeVertex(cell.vertices[i], cell);
        float3 diff = pc - seed;
        float d2 = dot(diff, diff);
        vDist = max(vDist, d2);
    }

    float3 dn = lastNeig.xyz - seed;
    float d2n = dot(dn, dn);
    return d2n > 4.0f * vDist;
}

static inline float4 CCExportBaryVolume(in ConvexCell cell, float3 seed)
{
    float4 bary = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 C = float4(seed, 1.0f);

    for (uint ve = 0; ve < cell.vertexCount; ve++)
    {
        float3 A3 = computeVertex(cell.vertices[ve], cell);
        float4 A = float4(A3, 1.0f);
        float4 AC = C - A;

        uint3 planesId = cell.vertices[ve];
        float4 planes[3] = { CCPlane(cell, planesId.x), CCPlane(cell, planesId.y), CCPlane(cell, planesId.z) };

        float4 APlaneI[3];
        for (int i = 0; i < 3; i++)
        {
            float3 ni = planes[i].xyz;
            float dotACni = dot(AC.xyz, ni);
            float dotnini = dot(ni, ni);
            if (dotnini < 1e-30f) APlaneI[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            else
            {
                float3 normalComp = (dotACni / dotnini) * ni;
                APlaneI[i] = AC - float4(normalComp, 0.0f);
            }
        }

        float4 ALineII1[3];
        for (int i = 0; i < 3; i++)
        {
            float3 ninj = cross(planes[i].xyz, planes[(i + 1) % 3].xyz);
            float ninj2 = dot(ninj, ninj);
            float lhs = VOLUME_EPSILON2 * dot(planes[i].xyz, planes[i].xyz) * dot(planes[(i + 1) % 3].xyz, planes[(i + 1) % 3].xyz);
            if (ninj2 < lhs) ALineII1[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            else ALineII1[i] = (dot(AC.xyz, ninj) / ninj2) * float4(ninj, 0.0f);
        }

        for (int i = 0; i < 3; i++)
        {
            for (int st = 0; st < 2; st++)
            {
                int j = (i + 3 - st) % 3;
                float w = (2 * st - 1) * det3x3(
                    AC.x, AC.y, AC.z,
                    APlaneI[i].x, APlaneI[i].y, APlaneI[i].z,
                    ALineII1[j].x, ALineII1[j].y, ALineII1[j].z);

                bary.w += w;
                float4 sum = AC + APlaneI[i] + ALineII1[j];
                bary.x += w * (A.x + 0.25f * sum.x);
                bary.y += w * (A.y + 0.25f * sum.y);
                bary.z += w * (A.z + 0.25f * sum.z);
            }
        }
    }

    return bary;
}

static inline void CCExportArea(
    in ConvexCell cell,
    float3 seed,
    uint staticPlaneCount,
    inout float surfIntr[P_MAX],
    float factor)
{
    float4 C = float4(seed, 1.0f);

    for (uint ve = 0; ve < cell.vertexCount; ve++)
    {
        float3 A3 = computeVertex(cell.vertices[ve], cell);
        float4 A = float4(A3, 1.0f);
        float4 AC = C - A;

        uint3 planesId = cell.vertices[ve];
        float4 planes[3] = { CCPlane(cell, planesId.x), CCPlane(cell, planesId.y), CCPlane(cell, planesId.z) };

        float4 APlaneI[3];
        for (int i = 0; i < 3; i++)
        {
            float3 ni = planes[i].xyz;
            float dotACni = dot(AC.xyz, ni);
            float dotnini = dot(ni, ni);
            if (dotnini < 1e-30f) APlaneI[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            else
            {
                float3 normalComp = (dotACni / dotnini) * ni;
                APlaneI[i] = AC - float4(normalComp, 0.0f);
            }
        }

        float4 ALineII1[3];
        for (int i = 0; i < 3; i++)
        {
            float3 ninj = cross(planes[i].xyz, planes[(i + 1) % 3].xyz);
            float ninj2 = dot(ninj, ninj);
            float lhs = VOLUME_EPSILON2 * dot(planes[i].xyz, planes[i].xyz) * dot(planes[(i + 1) % 3].xyz, planes[(i + 1) % 3].xyz);
            if (ninj2 < lhs) ALineII1[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            else ALineII1[i] = (dot(AC.xyz, ninj) / ninj2) * float4(ninj, 0.0f);
        }

        for (int i = 0; i < 3; i++)
        {
            for (int st = 0; st < 2; st++)
            {
                int j = (i + 3 - st) % 3;
                uint planeId = (i == 0) ? planesId.x : ((i == 1) ? planesId.y : planesId.z);
                
                if (planeId >= 6 && planeId < staticPlaneCount && planeId < P_MAX)
                {
                    double triAreaDet = det3x3D(
                        (double)APlaneI[i].x, (double)ALineII1[j].x, (double)planes[i].x,
                        (double)APlaneI[i].y, (double)ALineII1[j].y, (double)planes[i].y,
                        (double)APlaneI[i].z, (double)ALineII1[j].z, (double)planes[i].z);

                    // Signed interface area contribution in world units
                    float triArea = float((2.0 * (double)st - 1.0) * triAreaDet / 2.0) * factor;
                    surfIntr[planeId] += triArea;
                }
            }
        }
    }
}

static inline bool intersectSegmentTriangle(
    float3 p0,
    float3 p1,
    float3 v0,
    float3 v1,
    float3 v2)
{
    const float eps = 1e-6f;

    float3 dir = p1 - p0;
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;

    float3 pvec = cross(dir, e2);
    float det = dot(e1, pvec);
    if (abs(det) < eps)
        return false;

    float invDet = 1.0f / det;
    float3 tvec = p0 - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < -eps || u > 1.0f + eps)
        return false;

    float3 qvec = cross(tvec, e1);
    float v = dot(dir, qvec) * invDet;
    if (v < -eps || u + v > 1.0f + eps)
        return false;

    float t = dot(e2, qvec) * invDet;
    return t > eps && t < 1.0f - eps;
}

static inline uint countSegmentMeshIntersections(float3 outsidePoint, float3 queryPoint)
{
    uint hitCount = 0u;
    uint triangleCount = 0u;
    uint triangleStride = 0u;
    meshTriangles.GetDimensions(triangleCount, triangleStride);
    for (uint triIdx = 0u; triIdx < triangleCount; triIdx++)
    {
        MeshTriangle tri = meshTriangles[triIdx];
        if (intersectSegmentTriangle(outsidePoint, queryPoint, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz))
        {
            hitCount++;
        }
    }
    return hitCount;
}

static inline bool isPointInsideMeshFallback(float3 queryPoint, float3 bboxMinW, float3 bboxMaxW)
{
    float3 bboxSize = bboxMaxW - bboxMinW;
    float expand = max(length(bboxSize) * 2.0f, 1.0f);

    float3 outsideA = bboxMaxW + expand * float3(1.0f, 1.0f, 1.0f);
    float3 outsideB = bboxMaxW + expand * float3(1.731f, 0.941f, 1.217f);
    float3 outsideC = bboxMinW - expand * float3(1.113f, 1.357f, 0.927f);

    uint insideVotes = 0u;
    insideVotes += (countSegmentMeshIntersections(outsideA, queryPoint) & 1u);
    insideVotes += (countSegmentMeshIntersections(outsideB, queryPoint) & 1u);
    insideVotes += (countSegmentMeshIntersections(outsideC, queryPoint) & 1u);
    return insideVotes >= 2u;
}

static float computeRestrictedVolume(uint cellID, float3 seed)
{
    int debugSlot = -1;
    bool debugThisCell = false;
    if (pushConstants.debugEnable != 0u)
    {
        debugSlot = getDebugSlot(cellID);
        debugThisCell = (debugSlot >= 0);
    }

    if (debugThisCell)
    {
        dumpInfo[debugSlot].cellID = cellID;
        dumpInfo[debugSlot].seedPos = float4(seed, 1.0f);
        dumpInfo[debugSlot].planeAreaCount = 0u;
        dumpInfo[debugSlot].unrestrictedVolume = 0.0f;
        dumpInfo[debugSlot].restrictedVolume = 0.0f;
    }

    // Init convex cell as a cube in canonical/grid space, then work in world space
    float canonicalVox = canonicalVoxelSize();

    float3 bboxMinW = worldFromCanonical(float3(-0.01f, -0.01f, -0.01f));
    float3 bboxMaxW = worldFromCanonical(float3(CANONICAL_DOMAIN_SIZE + 0.01f, CANONICAL_DOMAIN_SIZE + 0.01f, CANONICAL_DOMAIN_SIZE + 0.01f));

    ConvexCell cell;

    // 8 bbox vertices as plane triplets
    cell.vertexCount = 8;
    cell.vertices[0] = uint3(2, 5, 0);
    cell.vertices[1] = uint3(5, 3, 0);
    cell.vertices[2] = uint3(1, 5, 2);
    cell.vertices[3] = uint3(5, 1, 3);
    cell.vertices[4] = uint3(4, 2, 0);
    cell.vertices[5] = uint3(4, 0, 3);
    cell.vertices[6] = uint3(2, 4, 1);
    cell.vertices[7] = uint3(4, 3, 1);

    // 6 bbox planes in world space
    CCSetPlane(cell, 0, float4( 1.0f,  0.0f,  0.0f, -bboxMinW.x));
    CCSetPlane(cell, 1, float4(-1.0f,  0.0f,  0.0f,  bboxMaxW.x));
    CCSetPlane(cell, 2, float4( 0.0f,  1.0f,  0.0f, -bboxMinW.y));
    CCSetPlane(cell, 3, float4( 0.0f, -1.0f,  0.0f,  bboxMaxW.y));
    CCSetPlane(cell, 4, float4( 0.0f,  0.0f,  1.0f, -bboxMinW.z));
    CCSetPlane(cell, 5, float4( 0.0f,  0.0f, -1.0f,  bboxMaxW.z));

    cell.planeCount = 6;

    int planeNeighborIDs[P_MAX];
    for (uint i = 0; i < P_MAX; i++) planeNeighborIDs[i] = -1;

    uint baseIdx = cellID * K_MAX_NEIGHBORS;

    for (uint i = 0; i < K_MAX_NEIGHBORS; i++)
    {
        uint neigh = neighborIndices[baseIdx + i];
        if (neigh == 0xFFFFFFFF) break;

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
        planeNeighborIDs[planeIdx] = (int)neigh;
        cell.planeCount++;

        CCClipByPlane(cell, planeIdx);
        if (cell.vertexCount < 1) break;

        if (CCIsSecurityRadiusReached(cell, seedPositions[neigh], seed)) break;

    }

    if (cell.planeCount >= P_MAX) return 0.0f;
    uint staticPlaneCount = cell.planeCount;

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

    bool foundInside = false;
    bool foundOutside = false;
    float3 PtInside = float3(0.0f, 0.0f, 0.0f);
    float3 PtOutside = float3(0.0f, 0.0f, 0.0f);

    int3 cornerMin = voxelMin;
    int3 cornerMax = min(voxelMax + int3(1, 1, 1), voxelParams.gridDim);

    for (int z = cornerMin.z; z <= cornerMax.z; z++)
    {
        for (int y = cornerMin.y; y <= cornerMax.y; y++)
        {
            for (int x = cornerMin.x; x <= cornerMax.x; x++)
            {
                int3 c = int3(x, y, z);
                int idx = cornerIndex(c);
                uint occ = occupancy[idx];

                if (occ > 2u) 
                occ = 0u; 

                if (occ == 2u && !foundInside)
                {
                    PtInside = worldFromCanonical(float3(c) * canonicalVox);
                    foundInside = true;
                }
                else if (occ == 0u && !foundOutside)
                {
                    PtOutside = worldFromCanonical(float3(c) * canonicalVox);
                    foundOutside = true;
                }
            }
        }
    }

    bool foundNonBorder = foundInside || foundOutside;
    uint inDomain = foundInside ? 2u : (foundOutside ? 0u : 1u);
    float3 origin = foundInside ? PtInside : PtOutside;

    if (inDomain == 1u)
    {
        int maxCornerX = voxelParams.gridDim.x;
        int maxCornerY = voxelParams.gridDim.y;
        int maxCornerZ = voxelParams.gridDim.z;

        int xMax = min(voxelMax.x + 1, maxCornerX);
        int yMax = min(voxelMax.y + 1, maxCornerY);
        int zMax = min(voxelMax.z + 1, maxCornerZ);

        int z = voxelMax.z + 1;
        if (z <= maxCornerZ)
        {
            for (int y = voxelMin.y; y <= yMax && inDomain == 1u; y++)
            {
                for (int x = voxelMin.x; x <= xMax; x++)
                {
                    int idx = cornerIndex(int3(x, y, z));
                    uint occ = occupancy[idx];
                    if (occ > 2u) occ = 0u;
                    if (occ != 1u)
                    {
                        inDomain = occ;
                        origin = worldFromCanonical(float3(x, y, z) * canonicalVox);
                        foundNonBorder = true;
                        break;
                    }
                }
            }
        }
        else
        {
            inDomain = 0u;
            origin = worldFromCanonical(float3(voxelMin.x, voxelMin.y, maxCornerZ) * canonicalVox);
            foundNonBorder = true;
        }

        if (inDomain == 1u)
        {
            int y = voxelMax.y + 1;
            if (y <= maxCornerY)
            {
                for (int z2 = voxelMin.z; z2 <= zMax && inDomain == 1u; z2++)
                {
                    for (int x = voxelMin.x; x <= xMax; x++)
                    {
                        int idx = cornerIndex(int3(x, y, z2));
                        uint occ = occupancy[idx];
                        if (occ > 2u) occ = 0u;
                        if (occ != 1u)
                        {
                            inDomain = occ;
                            origin = worldFromCanonical(float3(x, y, z2) * canonicalVox);
                            foundNonBorder = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                inDomain = 0u;
                origin = worldFromCanonical(float3(voxelMin.x, maxCornerY, voxelMin.z) * canonicalVox);
                foundNonBorder = true;
            }
        }

        if (inDomain == 1u)
        {
            int x = voxelMax.x + 1;
            if (x <= maxCornerX)
            {
                for (int z2 = voxelMin.z; z2 <= zMax && inDomain == 1u; z2++)
                {
                    for (int y2 = voxelMin.y; y2 <= yMax; y2++)
                    {
                        int idx = cornerIndex(int3(x, y2, z2));
                        uint occ = occupancy[idx];
                        if (occ > 2u) occ = 0u;
                        if (occ != 1u)
                        {
                            inDomain = occ;
                            origin = worldFromCanonical(float3(x, y2, z2) * canonicalVox);
                            foundNonBorder = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                inDomain = 0u;
                origin = worldFromCanonical(float3(maxCornerX, voxelMin.y, voxelMin.z) * canonicalVox);
                foundNonBorder = true;
            }
        }
    }

    bool addVolume = (inDomain == 2u);

    uint flags = seedFlags[cellID];
    bool isGhost = (flags & 1u) != 0u;

    float4 volBary = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float unrestrictedVolume = 0.0f;
    float3 unrestrictedCentroid = seed;
    bool hasUnrestrictedCentroid = false;

    float planeAreas[P_MAX];
    for (uint p = 0; p < staticPlaneCount && p < P_MAX; p++) planeAreas[p] = 0.0f;

    if (!isGhost)
    {
        float4 un = CCExportBaryVolume(cell, seed);
        unrestrictedVolume = un.w / 6.0f;
        if (abs(un.w) > 1e-20f)
        {
            unrestrictedCentroid = un.xyz / un.w;
            hasUnrestrictedCentroid = true;
        }
        if (addVolume) volBary = un / 6.0f;
        
        CCExportArea(cell, seed, staticPlaneCount, planeAreas, 1.0f);

        if (pushConstants.debugEnable != 0u)
        {
            exportUnrestrictedCell(cellID, cell, unrestrictedVolume);
        }
    }

    const int MAX_TRI = 128;
    int uniqueTri[MAX_TRI];
    uint uniqueCount = 0;

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
                    for (uint j = 0; j < uniqueCount; j++) { if (uniqueTri[j] == triId) { found = true; break; } }
                    if (!found) uniqueTri[uniqueCount++] = triId;
                }
            }
        }
    }

    if (debugThisCell)
    {
        dumpInfo[debugSlot].unrestrictedVolume = unrestrictedVolume;
    }

    nodes[cellID].interfaceNeighborCount = 0;

    if (!foundNonBorder && uniqueCount > 0)
    {
        float3 bboxSize = bboxMaxW - bboxMinW;
        float expand = max(length(bboxSize) * 2.0f, 1.0f);
        float3 fallbackOrigin = bboxMaxW + expand * float3(1.0f, 1.0f, 1.0f);

        bool seedInside = isPointInsideMeshFallback(seed, bboxMinW, bboxMaxW);
        bool centroidInside = hasUnrestrictedCentroid &&
                              isPointInsideMeshFallback(unrestrictedCentroid, bboxMinW, bboxMaxW);

        if (seedInside || centroidInside)
        {
            origin = fallbackOrigin;
            inDomain = 0u;
            foundNonBorder = true;
            addVolume = false;
        }
        else
        {
            if (debugThisCell) dumpInfo[debugSlot].restrictedVolume = 0.0f;
            return 0.0f;
        }
    }
    
    ConvexCellBackup backup;
    CCBUInit(backup, cell);

    for (uint i = 0; i < uniqueCount; i++)
    {
        int triIdx = uniqueTri[i];
        MeshTriangle tri = meshTriangles[triIdx];

        float3 v0 = tri.v0.xyz;
        float3 v1 = tri.v1.xyz;
        float3 v2 = tri.v2.xyz;

        float triDet = det3x3(
            v0.x - origin.x, v0.y - origin.y, v0.z - origin.z,
            v1.x - origin.x, v1.y - origin.y, v1.z - origin.z,
            v2.x - origin.x, v2.y - origin.y, v2.z - origin.z);

        bool goOut = triDet > 0.0f;

        if (!goOut)
        {
            float3 tmp = v2;
            v2 = v1;
            v1 = tmp;
        }

        if (cell.planeCount + 4 >= P_MAX) { CCBURestore(backup, cell); cell.planeCount = staticPlaneCount; continue; }

        CCSetPlane(cell, cell.planeCount++, getPlaneFromPoints(origin, v0, v1));
        CCClipByPlane(cell, cell.planeCount - 1);
        if (cell.vertexCount < 1) { CCBURestore(backup, cell); cell.planeCount = staticPlaneCount; continue; }

        CCSetPlane(cell, cell.planeCount++, getPlaneFromPoints(origin, v2, v0));
        CCClipByPlane(cell, cell.planeCount - 1);
        if (cell.vertexCount < 1) { CCBURestore(backup, cell); cell.planeCount = staticPlaneCount; continue; }

        CCSetPlane(cell, cell.planeCount++, getPlaneFromPoints(origin, v1, v2));
        CCClipByPlane(cell, cell.planeCount - 1);
        if (cell.vertexCount < 1) { CCBURestore(backup, cell); cell.planeCount = staticPlaneCount; continue; }

        CCSetPlane(cell, cell.planeCount++, getPlaneFromPoints(v1, v2, v0));
        CCClipByPlane(cell, cell.planeCount - 1);
        if (cell.vertexCount < 1) { CCBURestore(backup, cell); cell.planeCount = staticPlaneCount; continue; }

        float factor = (inDomain == 2u) ? (goOut ? -1.0f : 1.0f) : (goOut ? 1.0f : -1.0f);

        float4 chunkBary = CCExportBaryVolume(cell, seed) * (factor / 6.0f);
        CCExportArea(cell, seed, staticPlaneCount, planeAreas, factor);

        volBary += chunkBary;

        CCBURestore(backup, cell);
        cell.planeCount = staticPlaneCount;
    }

    float restrictedVolume = volBary.w;

    if (debugThisCell)
    {
        dumpInfo[debugSlot].restrictedVolume = restrictedVolume;
    }

    for (uint p = 6; p < staticPlaneCount && p < P_MAX; p++)
    {
        int globalNeigh = planeNeighborIDs[p];

        if (globalNeigh < 0) 
        continue;

        float area = planeAreas[p];

        if (debugThisCell)
        {
            uint debugPlaneIdx = dumpInfo[debugSlot].planeAreaCount;
            if (debugPlaneIdx < DEBUG_MAX_PLANE_AREAS)
            {
                dumpInfo[debugSlot].planeAreas[debugPlaneIdx].planeIndex = p;
                dumpInfo[debugSlot].planeAreas[debugPlaneIdx].neighborCellID = (uint)globalNeigh;
                dumpInfo[debugSlot].planeAreas[debugPlaneIdx].area = area;
                dumpInfo[debugSlot].planeAreas[debugPlaneIdx]._padding = 0.0f;
                dumpInfo[debugSlot].planeAreaCount = debugPlaneIdx + 1u;
            }
        }
         
        if (abs(area) < 1e-16f) 
        continue;

        uint writeIdx = nodes[cellID].interfaceNeighborCount;
        if (writeIdx < 50)
        {
            uint flatIdx = cellID * 50 + writeIdx;
            interfaceAreas[flatIdx] = area; 
            interfaceNeighborIds[flatIdx] = (uint)globalNeigh;
            nodes[cellID].interfaceNeighborCount++;
        }
    }
    return restrictedVolume;
}

[numthreads(32, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint localNodeID = dtid.x;
    if (localNodeID >= pushConstants.nodeCount)
    {
        return;
    }
    uint nodeID = pushConstants.nodeOffset + localNodeID;

    float3 seed = seedPositions[nodeID].xyz;
    float vol = computeRestrictedVolume(nodeID, seed);

    uint flags = seedFlags[nodeID];
    bool isGhost = (flags & 1u) != 0u;

    // Precompute conductivity per thermal mass for the diffusion pass hot loop.
    nodes[nodeID].thermalMass = nodes[nodeID].density * nodes[nodeID].specificHeat * abs(vol);
    if (nodes[nodeID].thermalMass > 1e-20f)
        nodes[nodeID].conductivityPerMass = nodes[nodeID].conductivity / nodes[nodeID].thermalMass;
    else
        nodes[nodeID].conductivityPerMass = 0.0f;

    if (!isGhost) nodes[nodeID].volume = vol;
    else nodes[nodeID].volume = 0.0f;
}
