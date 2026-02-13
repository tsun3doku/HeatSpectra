struct SurfacePoint
{
    float3 position;
    float  temperature;
    float3 normal;
    float  area;
    float4 color;
};

struct HeatSourceTriangle
{
    float4 centerArea;
    float4 normalPad;
    uint4  indices;
};

struct GridPoint
{
    float4 position;
    float4 normal;
    float  temperature;
    float  area;
    uint   sourceIndex;
    uint   isHeatSource;
};

struct HashGridParams
{
    float3 gridMin;
    float  cellSize;
    int3   gridDimensions;
    uint   maxPointsPerCell;
    uint   totalCells;
};

struct SurfelParams
{
    float thermalConductance;
    float contactPressure;
    float frictionCoeff;
    float padding;
};

struct VoronoiSurfaceMapping
{
    uint cellIndex;
    uint _padding0;
    uint _padding1;
    uint _padding2;
};

struct ContactSampleGPU
{
    uint  sourceTriangleIndex;
    float u;
    float v;
    float wArea;
};

struct ContactPairGPU
{
    ContactSampleGPU samples[7];
    float contactArea;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct PushConstants
{
    float4x4 heatSourceModelMatrix;
    float4x4 visModelMatrix;
    float4x4 inverseHeatSourceModelMatrix;
    uint maxNodeNeighbors;
    uint substepIndex;
    uint _pad0;
    uint _pad1;
};

[[vk::binding(0)]] StructuredBuffer<uint> nodeTemperatureReadBuffer;
[[vk::binding(1)]] StructuredBuffer<SurfacePoint> receiverCentroidBuffer;
[[vk::binding(2)]] StructuredBuffer<uint> receiverTriangleIndexBuffer;
[[vk::binding(3)]] StructuredBuffer<SurfacePoint> heatSourceBuffer;
[[vk::binding(4)]] StructuredBuffer<HeatSourceTriangle> heatSourceTriangleBuffer;
[[vk::binding(5)]] StructuredBuffer<GridPoint> hashGridBuffer;
[[vk::binding(6)]] StructuredBuffer<uint> cellCountBuffer;

[[vk::binding(7)]] cbuffer HashGridParamsBuffer
{
    HashGridParams gridParams;
};

[[vk::binding(8)]] cbuffer SurfelParamsBuffer
{
    SurfelParams surfelParams;
};

[[vk::binding(9)]] StructuredBuffer<VoronoiSurfaceMapping> voronoiMappingBuffer;
[[vk::binding(10)]] RWStructuredBuffer<uint> injectionKBuffer;
[[vk::binding(11)]] RWStructuredBuffer<uint> injectionKTBuffer;

[[vk::binding(12)]] StructuredBuffer<ContactPairGPU> contactPairBuffer;

[[vk::push_constant]] cbuffer PushConstantsBuffer
{
    PushConstants pushConstants;
};

void atomicAddFloatK(uint idx, float addVal)
{
    uint oldVal = injectionKBuffer[idx];
    while (true)
    {
        float oldF = asfloat(oldVal);
        float newF = oldF + addVal;
        uint newVal = asuint(newF);

        uint prevVal = 0;
        InterlockedCompareExchange(injectionKBuffer[idx], oldVal, newVal, prevVal);
        if (prevVal == oldVal)
            break;

        oldVal = prevVal;
    }
}

void atomicAddFloatKT(uint idx, float addVal)
{
    uint oldVal = injectionKTBuffer[idx];
    while (true)
    {
        float oldF = asfloat(oldVal);
        float newF = oldF + addVal;
        uint newVal = asuint(newF);

        uint prevVal = 0;
        InterlockedCompareExchange(injectionKTBuffer[idx], oldVal, newVal, prevVal);
        if (prevVal == oldVal)
            break;

        oldVal = prevVal;
    }
}

uint getCellIndex(int3 gridCoord)
{
    int3 clamped = clamp(gridCoord, int3(0, 0, 0), gridParams.gridDimensions - int3(1, 1, 1));
    return uint(clamped.x) + uint(clamped.y) * uint(gridParams.gridDimensions.x) + uint(clamped.z) * uint(gridParams.gridDimensions.x) * uint(gridParams.gridDimensions.y);
}

int3 modelToGrid(float3 modelPos)
{
    float3 gridPos = modelPos - gridParams.gridMin;
    return int3(floor(gridPos / gridParams.cellSize));
}

void queryNearbyHeatSource(float3 surfacePosWorld, float3 receiverNormalWorld, out float outTemp, out float outContactStrength)
{
    float3 receiverModelPos = mul(pushConstants.inverseHeatSourceModelMatrix, float4(surfacePosWorld, 1.0f)).xyz;

    float3x3 invModel3x3 = (float3x3)pushConstants.inverseHeatSourceModelMatrix;
    float3 receiverModelNormal = normalize(mul(invModel3x3, receiverNormalWorld));

    const float contactSearchRadius = 0.002f;
    int3 centerCell = modelToGrid(receiverModelPos);
    int searchRadius = int(ceil(contactSearchRadius / gridParams.cellSize)) + 1;

    float bestScore = -1.0f;
    float bestTemp = 0.0f;

    uint triCount = 0;
    uint triStride = 0;
    heatSourceTriangleBuffer.GetDimensions(triCount, triStride);

    uint sourceVertexCount = 0;
    uint sourceVertexStride = 0;
    heatSourceBuffer.GetDimensions(sourceVertexCount, sourceVertexStride);

    for (int dz = -searchRadius; dz <= searchRadius; dz++)
    {
        for (int dy = -searchRadius; dy <= searchRadius; dy++)
        {
            for (int dx = -searchRadius; dx <= searchRadius; dx++)
            {
                int3 cellCoord = centerCell + int3(dx, dy, dz);

                if (any(cellCoord < int3(0, 0, 0)) || any(cellCoord >= gridParams.gridDimensions))
                {
                    continue;
                }

                uint cellIdx = getCellIndex(cellCoord);
                uint cellCount = cellCountBuffer[cellIdx];
                uint baseIdx = cellIdx * gridParams.maxPointsPerCell;

                uint iterCount = min(cellCount, gridParams.maxPointsPerCell);
                for (uint i = 0; i < iterCount; i++)
                {
                    GridPoint gp = hashGridBuffer[baseIdx + i];

                    if (gp.isHeatSource == 1u)
                    {
                        uint triId = gp.sourceIndex;
                        if (triId >= triCount)
                        {
                            continue;
                        }

                        HeatSourceTriangle tri = heatSourceTriangleBuffer[triId];
                        uint i0 = tri.indices.x;
                        uint i1 = tri.indices.y;
                        uint i2 = tri.indices.z;

                        if (i0 >= sourceVertexCount || i1 >= sourceVertexCount || i2 >= sourceVertexCount)
                        {
                            continue;
                        }

                        float3 a = heatSourceBuffer[i0].position;
                        float3 b = heatSourceBuffer[i1].position;
                        float3 c = heatSourceBuffer[i2].position;

                        float3 ab = b - a;
                        float3 ac = c - a;
                        float3 ap = receiverModelPos - a;

                        float d1 = dot(ab, ap);
                        float d2 = dot(ac, ap);
                        float3 closest;
                        float3 bary;

                        if (d1 <= 0.0f && d2 <= 0.0f)
                        {
                            closest = a;
                            bary = float3(1.0f, 0.0f, 0.0f);
                        }
                        else
                        {
                            float3 bp = receiverModelPos - b;
                            float d3 = dot(ab, bp);
                            float d4 = dot(ac, bp);

                            if (d3 >= 0.0f && d4 <= d3)
                            {
                                closest = b;
                                bary = float3(0.0f, 1.0f, 0.0f);
                            }
                            else
                            {
                                float vc = d1 * d4 - d3 * d2;
                                if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
                                {
                                    float v = d1 / (d1 - d3);
                                    closest = a + v * ab;
                                    bary = float3(1.0f - v, v, 0.0f);
                                }
                                else
                                {
                                    float3 cp = receiverModelPos - c;
                                    float d5 = dot(ab, cp);
                                    float d6 = dot(ac, cp);

                                    if (d6 >= 0.0f && d5 <= d6)
                                    {
                                        closest = c;
                                        bary = float3(0.0f, 0.0f, 1.0f);
                                    }
                                    else
                                    {
                                        float vb = d5 * d2 - d1 * d6;
                                        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
                                        {
                                            float w = d2 / (d2 - d6);
                                            closest = a + w * ac;
                                            bary = float3(1.0f - w, 0.0f, w);
                                        }
                                        else
                                        {
                                            float va = d3 * d6 - d5 * d4;
                                            if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
                                            {
                                                float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                                                closest = b + w * (c - b);
                                                bary = float3(0.0f, 1.0f - w, w);
                                            }
                                            else
                                            {
                                                float denom = 1.0f / (va + vb + vc);
                                                float v = vb * denom;
                                                float w = vc * denom;
                                                closest = a + ab * v + ac * w;
                                                bary = float3(1.0f - v - w, v, w);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        float gap = length(receiverModelPos - closest);
                        if (gap > contactSearchRadius)
                        {
                            continue;
                        }

                        float3 triNormal = normalize(tri.normalPad.xyz);
                        float normalDot = dot(receiverModelNormal, triNormal);

                        float distScore = 1.0f - (gap / contactSearchRadius);
                        float orientScore = (1.0f - normalDot) * 0.5f;
                        float combinedScore = distScore * 0.6f + orientScore * 0.4f;

                        float t0 = heatSourceBuffer[i0].temperature;
                        float t1 = heatSourceBuffer[i1].temperature;
                        float t2 = heatSourceBuffer[i2].temperature;
                        float triTemp = bary.x * t0 + bary.y * t1 + bary.z * t2;

                        if (combinedScore > bestScore)
                        {
                            bestScore = combinedScore;
                            bestTemp = triTemp;
                        }
                    }
                }
            }
        }
    }

    if (bestScore > 0.0f)
    {
        outTemp = bestTemp;
        outContactStrength = bestScore;
    }
    else
    {
        outTemp = 0.0f;
        outContactStrength = 0.0f;
    }
}

void addWeightedInjection(uint cellIndex, float w, float effectiveTemp, float contactStrength, float area)
{
    if (w <= 0.0f)
    {
        return;
    }

    float nodeTemp = asfloat(nodeTemperatureReadBuffer[cellIndex]);
    float tempDiff = effectiveTemp - nodeTemp;

    if (tempDiff <= 0.0f)
    {
        return;
    }

    float h_eff = surfelParams.thermalConductance * contactStrength * (area * w);

    atomicAddFloatK(cellIndex, h_eff);
    atomicAddFloatKT(cellIndex, h_eff * effectiveTemp);
}

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint triID = dtid.x;

    uint centroidCount = 0;
    uint centroidStride = 0;
    receiverCentroidBuffer.GetDimensions(centroidCount, centroidStride);

    if (triID >= centroidCount)
    {
        return;
    }

    uint pairCount = 0;
    uint pairStride = 0;
    contactPairBuffer.GetDimensions(pairCount, pairStride);
    if (triID >= pairCount)
    {
        return;
    }

    ContactPairGPU pair = contactPairBuffer[triID];
    if (pair.contactArea <= 0.0f)
    {
        return;
    }

    float contactStrength = 1.0f;

    uint base = triID * 3u;

    uint indexCount = 0;
    uint indexStride = 0;
    receiverTriangleIndexBuffer.GetDimensions(indexCount, indexStride);

    if (base + 2u >= indexCount)
    {
        return;
    }

    uint v0 = receiverTriangleIndexBuffer[base + 0u];
    uint v1 = receiverTriangleIndexBuffer[base + 1u];
    uint v2 = receiverTriangleIndexBuffer[base + 2u];

    uint c0 = voronoiMappingBuffer[v0].cellIndex;
    uint c1 = voronoiMappingBuffer[v1].cellIndex;
    uint c2 = voronoiMappingBuffer[v2].cellIndex;

    static const float3 recvBary[7] = {
        float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f),
        float3(0.0597158717f, 0.4701420641f, 0.4701420641f),
        float3(0.4701420641f, 0.0597158717f, 0.4701420641f),
        float3(0.4701420641f, 0.4701420641f, 0.0597158717f),
        float3(0.7974269853f, 0.1012865073f, 0.1012865073f),
        float3(0.1012865073f, 0.7974269853f, 0.1012865073f),
        float3(0.1012865073f, 0.1012865073f, 0.7974269853f),
    };

    for (uint si = 0; si < 7u; ++si)
    {
        ContactSampleGPU s = pair.samples[si];
        if (s.sourceTriangleIndex == 0xFFFFFFFFu || s.wArea <= 0.0f)
        {
            continue;
        }

        HeatSourceTriangle tri = heatSourceTriangleBuffer[s.sourceTriangleIndex];
        uint i0 = tri.indices.x;
        uint i1 = tri.indices.y;
        uint i2 = tri.indices.z;

        float w0s = 1.0f - s.u - s.v;
        float w1s = s.u;
        float w2s = s.v;

        float t0 = heatSourceBuffer[i0].temperature;
        float t1 = heatSourceBuffer[i1].temperature;
        float t2 = heatSourceBuffer[i2].temperature;
        float effectiveTemp = w0s * t0 + w1s * t1 + w2s * t2;

        float area = s.wArea;
        float3 bw = recvBary[si];

        addWeightedInjection(c0, bw.x, effectiveTemp, contactStrength, area);
        addWeightedInjection(c1, bw.y, effectiveTemp, contactStrength, area);
        addWeightedInjection(c2, bw.z, effectiveTemp, contactStrength, area);
    }
}
