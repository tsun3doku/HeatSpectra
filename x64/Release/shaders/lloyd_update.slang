static const uint K_MAX_NEIGHBORS = 50;

struct SpatialInfoGPU
{
    uint offset;
    uint count;
    uint pad0;
    uint pad1;
};

struct LloydParams
{
    uint  nodeCount;
    float alpha;
    float maxStep;
    float pad1;
};

[[vk::binding(0)]] RWStructuredBuffer<float4> seedPositions;
[[vk::binding(5)]] StructuredBuffer<uint>     seedFlags;
[[vk::binding(6)]] StructuredBuffer<float4>   lloydAccum;
[[vk::binding(8)]] cbuffer LloydParamsBuffer { LloydParams params; };

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint cellID = dtid.x;
    if (cellID >= params.nodeCount) return;

    uint flags = seedFlags[cellID];
    bool isGhost = (flags & 1u) != 0u;

    if (isGhost) return;

    float4 acc = lloydAccum[cellID];
    float area = acc.w;
    if (area <= 1e-20f) return;

    float3 centroid = acc.xyz / area;

    float4 sp = seedPositions[cellID];
    float3 seed = sp.xyz;

    float3 delta = centroid - seed;
    float lenD = length(delta);
    if (lenD <= 1e-10f) return;

    float step = params.alpha;
    float maxStep = params.maxStep;

    float3 move = delta * step;

    float moveLen = length(move);
    if (maxStep > 0.0f && moveLen > maxStep)
    {
        move *= (maxStep / max(moveLen, 1e-20f));
    }

    seedPositions[cellID] = float4(seed + move, sp.w);
}
