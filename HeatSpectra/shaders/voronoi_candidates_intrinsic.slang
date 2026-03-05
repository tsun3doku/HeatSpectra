static const uint K_CANDIDATES = 64;

struct SurfacePoint
{
    float3 position;
    float temperature;
    float3 normal;
    float area;
    float4 color;
};

struct PushConstants
{
    uint faceCount;
    uint seedCount;
    uint seedOffset;
    uint _pad0;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0)]]
StructuredBuffer<SurfacePoint> vertexBuffer;

[[vk::binding(1)]]
StructuredBuffer<uint> faceIndexBuffer;

[[vk::binding(2)]]
StructuredBuffer<float4> seedPositions;

[[vk::binding(3)]]
RWStructuredBuffer<uint> outCandidates;

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint faceId = dispatchThreadID.x;
    if (faceId >= pc.faceCount)
    {
        return;
    }

    uint baseIndex = faceId * 3;
    uint3 indices = uint3(
        faceIndexBuffer[baseIndex + 0],
        faceIndexBuffer[baseIndex + 1],
        faceIndexBuffer[baseIndex + 2]
    );
    if (indices.x == 0xFFFFFFFFu || indices.y == 0xFFFFFFFFu || indices.z == 0xFFFFFFFFu)
    {
        uint base = faceId * K_CANDIDATES;
        [unroll]
        for (uint i = 0; i < K_CANDIDATES; ++i)
        {
            outCandidates[base + i] = 0xFFFFFFFFu;
        }
        return;
    }

    float3 p0 = vertexBuffer[indices.x].position;
    float3 p1 = vertexBuffer[indices.y].position;
    float3 p2 = vertexBuffer[indices.z].position;
    float3 center = (p0 + p1 + p2) / 3.0f;

    float bestDist[K_CANDIDATES];
    uint bestId[K_CANDIDATES];
    [unroll]
    for (uint i = 0; i < K_CANDIDATES; ++i)
    {
        bestDist[i] = 1e30f;
        bestId[i] = 0xFFFFFFFFu;
    }

    for (uint s = 0; s < pc.seedCount; ++s)
    {
        uint globalSeedIndex = pc.seedOffset + s;
        float3 sp = seedPositions[globalSeedIndex].xyz;
        float d0 = dot(p0 - sp, p0 - sp);
        float d1 = dot(p1 - sp, p1 - sp);
        float d2 = dot(p2 - sp, p2 - sp);
        float dc = dot(center - sp, center - sp);
        float distSq = min(min(d0, d1), min(d2, dc));

        if (distSq >= bestDist[K_CANDIDATES - 1])
        {
            continue;
        }

        uint insertPos = K_CANDIDATES - 1;
        while (insertPos > 0 && distSq < bestDist[insertPos - 1])
        {
            bestDist[insertPos] = bestDist[insertPos - 1];
            bestId[insertPos] = bestId[insertPos - 1];
            insertPos--;
        }

        bestDist[insertPos] = distSq;
        bestId[insertPos] = globalSeedIndex;
    }

    uint base = faceId * K_CANDIDATES;
    [unroll]
    for (uint i = 0; i < K_CANDIDATES; ++i)
    {
        outCandidates[base + i] = bestId[i];
    }
}
