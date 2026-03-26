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

struct ContactCellMap
{
    uint sampleIndex;
    float weight;
    uint _pad0;
    uint _pad1;
};

struct ContactCellRange
{
    uint cellIndex;
    uint startIndex;
    uint count;
    uint _pad0;
};

[[vk::binding(0)]] StructuredBuffer<uint> nodeTemperatureReadBuffer;
[[vk::binding(1)]] cbuffer HeatContactParamsBuffer
{
    HeatContactParams heatContactParams;
};

[[vk::binding(2)]] RWStructuredBuffer<uint> injectionKBuffer;
[[vk::binding(3)]] RWStructuredBuffer<uint> injectionKTBuffer;
[[vk::binding(4)]] StructuredBuffer<ContactSampleGPU> contactSampleBuffer;
[[vk::binding(5)]] StructuredBuffer<ContactCellMap> contactCellMapBuffer;
[[vk::binding(6)]] StructuredBuffer<ContactCellRange> contactCellRangeBuffer;
[[vk::binding(7)]] StructuredBuffer<uint> emitterTriangleIndexBuffer;
[[vk::binding(8)]] StructuredBuffer<VoronoiSurfaceMapping> emitterVoronoiMappingBuffer;

[[vk::push_constant]] cbuffer ContactPushConstants
{
    uint couplingKind;
    float heatSourceTemperature;
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
    uint rangeIndex = dtid.x;

    uint rangeCount = 0;
    uint rangeStride = 0;
    contactCellRangeBuffer.GetDimensions(rangeCount, rangeStride);
    if (rangeIndex >= rangeCount)
    {
        return;
    }

    ContactCellRange range = contactCellRangeBuffer[rangeIndex];
    if (range.count == 0u)
    {
        return;
    }

    uint nodeCount = 0;
    uint nodeStride = 0;
    nodeTemperatureReadBuffer.GetDimensions(nodeCount, nodeStride);
    if (range.cellIndex == 0xFFFFFFFFu || range.cellIndex >= nodeCount)
    {
        return;
    }

    float nodeTemp = asfloat(nodeTemperatureReadBuffer[range.cellIndex]);

    float sumK = 0.0f;
    float sumKT = 0.0f;
    float contactStrength = 1.0f;

    uint endIndex = range.startIndex + range.count;
    for (uint idx = range.startIndex; idx < endIndex; ++idx)
    {
        ContactCellMap entry = contactCellMapBuffer[idx];
        ContactSampleGPU s = contactSampleBuffer[entry.sampleIndex];
        if (s.sourceTriangleIndex == 0xFFFFFFFFu)
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
            effectiveTemp = contactPushConstants.heatSourceTemperature;
        }

        if (effectiveTemp <= nodeTemp)
        {
            continue;
        }

        float h_eff = heatContactParams.thermalConductance * contactStrength * entry.weight;
        sumK += h_eff;
        sumKT += h_eff * effectiveTemp;
    }

    if (sumK > 0.0f)
    {
        atomicAddFloatK(range.cellIndex, sumK);
        atomicAddFloatKT(range.cellIndex, sumKT);
    }
}
