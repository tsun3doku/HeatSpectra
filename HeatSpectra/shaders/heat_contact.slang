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

[[vk::binding(0)]] StructuredBuffer<uint> nodeTemperatureReadBuffer;
[[vk::binding(1)]] StructuredBuffer<uint> receiverTriangleIndexBuffer;
[[vk::binding(2)]] StructuredBuffer<SurfacePoint> heatSourceBuffer;
[[vk::binding(3)]] StructuredBuffer<HeatSourceTriangle> heatSourceTriangleBuffer;
[[vk::binding(4)]] cbuffer SurfelParamsBuffer
{
    SurfelParams surfelParams;
};

[[vk::binding(5)]] StructuredBuffer<VoronoiSurfaceMapping> voronoiMappingBuffer;
[[vk::binding(6)]] RWStructuredBuffer<uint> injectionKBuffer;
[[vk::binding(7)]] RWStructuredBuffer<uint> injectionKTBuffer;
[[vk::binding(8)]] StructuredBuffer<ContactPairGPU> contactPairBuffer;

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

void addWeightedInjection(uint cellIndex, uint nodeCount, float w, float effectiveTemp, float contactStrength, float area)
{
    if (cellIndex == 0xFFFFFFFFu || cellIndex >= nodeCount)
    {
        return;
    }

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
    uint triCountFromIndex = indexCount / 3u;
    if (triID >= triCountFromIndex)
    {
        return;
    }

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

    uint nodeCount = 0;
    uint nodeStride = 0;
    nodeTemperatureReadBuffer.GetDimensions(nodeCount, nodeStride);

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

        addWeightedInjection(c0, nodeCount, bw.x, effectiveTemp, contactStrength, area);
        addWeightedInjection(c1, nodeCount, bw.y, effectiveTemp, contactStrength, area);
        addWeightedInjection(c2, nodeCount, bw.z, effectiveTemp, contactStrength, area);
    }
}
