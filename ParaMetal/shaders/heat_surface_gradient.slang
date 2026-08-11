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

struct GMLSSurfaceGradientWeight
{
    uint cellIndex;
    float dTdxWeight;
    float dTdyWeight;
    float dTdzWeight;
};

struct HeatModelPushConstant {
    uint elementCount;
    float metersPerUnit;
};

[[vk::binding(0)]] StructuredBuffer<float> nodeTemperatureReadBuffer;
[[vk::binding(1)]] StructuredBuffer<SurfacePoint> surfaceBuffer;        // read positions/normals
[[vk::binding(2)]] RWStructuredBuffer<float4> surfaceGradientBuffer;    // write gradient colors here

[[vk::binding(10)]] StructuredBuffer<GMLSSurfaceStencil> gmlsSurfaceStencilBuffer;
[[vk::binding(12)]] StructuredBuffer<GMLSSurfaceGradientWeight> gmlsSurfaceGradientWeightBuffer;

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

    float3 gradientT = float3(0.0, 0.0, 0.0);

    uint stencilCount = 0;
    uint stencilStride = 0;
    gmlsSurfaceStencilBuffer.GetDimensions(stencilCount, stencilStride);
    if (vertexID >= stencilCount)
    {
        return;
    }

    GMLSSurfaceStencil stencil = gmlsSurfaceStencilBuffer[vertexID];
    if (stencil.gradientWeightCount == 0u)
    {
        return;
    }

    for (uint i = 0; i < stencil.gradientWeightCount; ++i)
    {
        GMLSSurfaceGradientWeight weight = gmlsSurfaceGradientWeightBuffer[stencil.gradientWeightOffset + i];
        float cellTemp = nodeTemperatureReadBuffer[weight.cellIndex];
        gradientT += float3(weight.dTdxWeight, weight.dTdyWeight, weight.dTdzWeight) * cellTemp;
    }
    gradientT /= pushConstants.metersPerUnit;

    SurfacePoint p = surfaceBuffer[vertexID];

    // Project 3D gradient onto surface tangent plane to get true surface heat flux
    float3 gradTangent = gradientT - dot(gradientT, p.normal) * p.normal;
    surfaceGradientBuffer[vertexID] = float4(-gradTangent, 1.0);
}
