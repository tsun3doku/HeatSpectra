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

struct HeatContactParams
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
[[vk::binding(4)]] cbuffer HeatContactParamsBuffer
{
    HeatContactParams heatContactParams;
};

[[vk::binding(5)]] StructuredBuffer<VoronoiSurfaceMapping> voronoiMappingBuffer;
[[vk::binding(6)]] RWStructuredBuffer<uint> injectionKBuffer;
[[vk::binding(7)]] RWStructuredBuffer<uint> injectionKTBuffer;
[[vk::binding(8)]] StructuredBuffer<ContactPairGPU> contactPairBuffer;
[[vk::binding(9)]] StructuredBuffer<uint> emitterTriangleIndexBuffer;
[[vk::binding(10)]] StructuredBuffer<VoronoiSurfaceMapping> emitterVoronoiMappingBuffer;

[[vk::push_constant]] cbuffer ContactPushConstants
{
    uint couplingKind;
    uint _pcPad0;
    uint _pcPad1;
    uint _pcPad2;
}
contactPushConstants;

static const uint contactCouplingSourceToReceiver = 0u;
static const uint contactCouplingReceiverToReceiver = 1u;

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

    float h_eff = heatContactParams.thermalConductance * contactStrength * (area * w);

    atomicAddFloatK(cellIndex, h_eff);
    atomicAddFloatKT(cellIndex, h_eff * effectiveTemp);
}

bool trySampleReceiverEmitterTemperature(ContactSampleGPU sample, uint nodeCount, out float effectiveTemp)
{
    uint emitterIndexCount = 0;
    uint emitterIndexStride = 0;
    emitterTriangleIndexBuffer.GetDimensions(emitterIndexCount, emitterIndexStride);
    uint emitterTriCount = emitterIndexCount / 3u;

    if (sample.sourceTriangleIndex >= emitterTriCount)
    {
        effectiveTemp = 0.0f;
        return false;
    }

    uint base = sample.sourceTriangleIndex * 3u;
    if (base + 2u >= emitterIndexCount)
    {
        effectiveTemp = 0.0f;
        return false;
    }

    uint e0 = emitterTriangleIndexBuffer[base + 0u];
    uint e1 = emitterTriangleIndexBuffer[base + 1u];
    uint e2 = emitterTriangleIndexBuffer[base + 2u];

    uint emitterMapCount = 0;
    uint emitterMapStride = 0;
    emitterVoronoiMappingBuffer.GetDimensions(emitterMapCount, emitterMapStride);

    if (e0 >= emitterMapCount || e1 >= emitterMapCount || e2 >= emitterMapCount)
    {
        effectiveTemp = 0.0f;
        return false;
    }

    uint c0 = emitterVoronoiMappingBuffer[e0].cellIndex;
    uint c1 = emitterVoronoiMappingBuffer[e1].cellIndex;
    uint c2 = emitterVoronoiMappingBuffer[e2].cellIndex;

    float w0 = 1.0f - sample.u - sample.v;
    float w1 = sample.u;
    float w2 = sample.v;

    float temperatureSum = 0.0f;
    float weightSum = 0.0f;

    if (c0 != 0xFFFFFFFFu && c0 < nodeCount)
    {
        temperatureSum += w0 * asfloat(nodeTemperatureReadBuffer[c0]);
        weightSum += w0;
    }
    if (c1 != 0xFFFFFFFFu && c1 < nodeCount)
    {
        temperatureSum += w1 * asfloat(nodeTemperatureReadBuffer[c1]);
        weightSum += w1;
    }
    if (c2 != 0xFFFFFFFFu && c2 < nodeCount)
    {
        temperatureSum += w2 * asfloat(nodeTemperatureReadBuffer[c2]);
        weightSum += w2;
    }

    if (weightSum <= 0.0f)
    {
        effectiveTemp = 0.0f;
        return false;
    }

    effectiveTemp = temperatureSum / weightSum;
    return true;
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

    uint receiverMapCount = 0;
    uint receiverMapStride = 0;
    voronoiMappingBuffer.GetDimensions(receiverMapCount, receiverMapStride);
    if (v0 >= receiverMapCount || v1 >= receiverMapCount || v2 >= receiverMapCount)
    {
        return;
    }

    uint c0 = voronoiMappingBuffer[v0].cellIndex;
    uint c1 = voronoiMappingBuffer[v1].cellIndex;
    uint c2 = voronoiMappingBuffer[v2].cellIndex;

    uint nodeCount = 0;
    uint nodeStride = 0;
    nodeTemperatureReadBuffer.GetDimensions(nodeCount, nodeStride);

    uint heatSourceVertexCount = 0;
    uint heatSourceVertexStride = 0;
    heatSourceBuffer.GetDimensions(heatSourceVertexCount, heatSourceVertexStride);

    uint heatSourceTriangleCount = 0;
    uint heatSourceTriangleStride = 0;
    heatSourceTriangleBuffer.GetDimensions(heatSourceTriangleCount, heatSourceTriangleStride);

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

        float effectiveTemp = 0.0f;
        if (contactPushConstants.couplingKind == contactCouplingReceiverToReceiver)
        {
            if (!trySampleReceiverEmitterTemperature(s, nodeCount, effectiveTemp))
            {
                continue;
            }
        }
        else
        {
            if (s.sourceTriangleIndex >= heatSourceTriangleCount)
            {
                continue;
            }

            HeatSourceTriangle tri = heatSourceTriangleBuffer[s.sourceTriangleIndex];
            uint i0 = tri.indices.x;
            uint i1 = tri.indices.y;
            uint i2 = tri.indices.z;
            if (i0 >= heatSourceVertexCount || i1 >= heatSourceVertexCount || i2 >= heatSourceVertexCount)
            {
                continue;
            }

            float w0s = 1.0f - s.u - s.v;
            float w1s = s.u;
            float w2s = s.v;

            float t0 = heatSourceBuffer[i0].temperature;
            float t1 = heatSourceBuffer[i1].temperature;
            float t2 = heatSourceBuffer[i2].temperature;
            effectiveTemp = w0s * t0 + w1s * t1 + w2s * t2;
        }

        float area = s.wArea;
        float3 bw = recvBary[si];

        addWeightedInjection(c0, nodeCount, bw.x, effectiveTemp, contactStrength, area);
        addWeightedInjection(c1, nodeCount, bw.y, effectiveTemp, contactStrength, area);
        addWeightedInjection(c2, nodeCount, bw.z, effectiveTemp, contactStrength, area);
    }
}
