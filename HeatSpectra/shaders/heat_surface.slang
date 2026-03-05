struct SurfacePoint
{
    float3 position;
    float  temperature;
    float3 normal;
    float  area;
    float4 color;
};

struct VoronoiSurfaceMapping
{
    uint cellIndex;
    uint _padding0;
    uint _padding1;
    uint _padding2;
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
[[vk::binding(1)]] RWStructuredBuffer<SurfacePoint> surfaceBuffer;

[[vk::binding(3)]] cbuffer TimeBuffer
{
    float deltaTime;
    float totalTime;
};

[[vk::binding(9)]] StructuredBuffer<VoronoiSurfaceMapping> voronoiMappingBuffer;

[[vk::push_constant]] cbuffer PushConstantsBuffer
{
    PushConstants pushConstants;
};

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint vertexID = dtid.x;

    uint surfaceCount = 0;
    uint surfaceStride = 0;
    surfaceBuffer.GetDimensions(surfaceCount, surfaceStride);

    if (vertexID >= surfaceCount) return;

    // Update display temperature in first substep
    if (pushConstants.substepIndex != 0u) return;

    uint nodeCount = 0;
    uint nodeStride = 0;
    nodeTemperatureReadBuffer.GetDimensions(nodeCount, nodeStride);

    uint cellIndex = voronoiMappingBuffer[vertexID].cellIndex;
    if (cellIndex == 0xFFFFFFFFu || cellIndex >= nodeCount)
    {
        return;
    }

    float displayTemp = asfloat(nodeTemperatureReadBuffer[cellIndex]);

    SurfacePoint p = surfaceBuffer[vertexID];
    p.temperature = displayTemp;
    surfaceBuffer[vertexID] = p;
}
