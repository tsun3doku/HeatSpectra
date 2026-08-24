struct SurfacePoint
{
    float3 position;
    float  temperatureC;
    float3 normal;
    float  vertexArea;
    float4 color;
};

struct GMLSSurfaceStencil
{
    uint valueWeightOffset;
    uint valueWeightCount;
    uint gradientWeightOffset;
    uint gradientWeightCount;
};

struct GMLSSurfaceWeight
{
    uint cellIndex;
    float weight;
};

struct BoundaryState
{
    uint conditionType;
    float temperatureC;
    float heatFlux;
    float heatTransferCoefficient;
};

struct HeatModelPushConstant {
    uint elementCount;
    float metersPerUnit;
};

[[vk::binding(0)]] StructuredBuffer<float> nodeTemperatureReadBuffer;
[[vk::binding(1)]] RWStructuredBuffer<SurfacePoint> surfaceBuffer;
[[vk::binding(8)]] StructuredBuffer<uint> surfaceBoundaryIndexBuffer;
[[vk::binding(9)]] StructuredBuffer<BoundaryState> boundaryStateBuffer;

[[vk::binding(10)]] StructuredBuffer<GMLSSurfaceStencil> gmlsSurfaceStencilBuffer;
[[vk::binding(11)]] StructuredBuffer<GMLSSurfaceWeight> gmlsSurfaceWeightBuffer;

[[vk::push_constant]] ConstantBuffer<HeatModelPushConstant> pushConstants;

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint vertexID = dtid.x;

    uint surfaceCount = 0;
    uint surfaceStride = 0;
    surfaceBuffer.GetDimensions(surfaceCount, surfaceStride);

    if (vertexID >= surfaceCount) return;

    uint nodeCount = 0;
    uint nodeStride = 0;
    nodeTemperatureReadBuffer.GetDimensions(nodeCount, nodeStride);

    float weightedTemperature = 0.0;
    float weightSum = 0.0;

    uint stencilCount = 0;
    uint stencilStride = 0;
    gmlsSurfaceStencilBuffer.GetDimensions(stencilCount, stencilStride);
    if (vertexID >= stencilCount)
    {
        return;
    }

    GMLSSurfaceStencil stencil = gmlsSurfaceStencilBuffer[vertexID];
    if (stencil.valueWeightCount == 0u)
    {
        SurfacePoint p = surfaceBuffer[vertexID];
        p.temperatureC = -1.0e30f;
        surfaceBuffer[vertexID] = p;
        return;
    }

    for (uint i = 0; i < stencil.valueWeightCount; ++i)
    {
        GMLSSurfaceWeight weight = gmlsSurfaceWeightBuffer[stencil.valueWeightOffset + i];
        weightedTemperature += weight.weight * nodeTemperatureReadBuffer[weight.cellIndex];
        weightSum += weight.weight;
    }

    if (abs(weightSum) <= 1e-8f)
    {
        SurfacePoint p = surfaceBuffer[vertexID];
        p.temperatureC = -1.0e30f;
        surfaceBuffer[vertexID] = p;
        return;
    }

    float temperatureC = weightedTemperature / weightSum;
    uint boundaryIndex = surfaceBoundaryIndexBuffer[vertexID];
    if (boundaryIndex != 0xffffffffu)
    {
        BoundaryState boundary = boundaryStateBuffer[boundaryIndex];
        if (boundary.conditionType == 1u)
        {
            temperatureC = boundary.temperatureC;
        }
    }

    SurfacePoint p = surfaceBuffer[vertexID];
    p.temperatureC = temperatureC;
    surfaceBuffer[vertexID] = p;
}
