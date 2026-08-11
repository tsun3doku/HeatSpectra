struct Node
{
    float volume;
    uint neighborOffset;
    uint neighborCount;
    uint interfaceNeighborCount;
};

struct NodeCoupling
{
    uint neighborNodeId;
    float conductance;
};

struct MaterialNode
{
    float conductivityPerMass;
    float thermalMass;
    float density;
    float specificHeat;
    float conductivity;
    float pad0;
    uint pad1;
    uint pad2;
};

struct BoundaryState
{
    uint conditionType;
    float temperatureC;
    float heatFlux;
    float heatTransferCoefficient;
};

struct BoundaryNode
{
    uint contributionOffset;
    uint contributionCount;
    uint dirichletStateIndex;
};

struct BoundaryContribution
{
    uint stateIndex;
    float area;
};

struct SimPlayback
{
    uint resetCounter;
    uint recordedTimelineFrames;
    uint timelineFrameCount;
    float deltaTime;
};

struct HeatModelPushConstant
{
    uint elementCount;
    float metersPerUnit;
};

[[vk::binding(0)]] StructuredBuffer<Node> simNodeBuffer;
[[vk::binding(1)]] StructuredBuffer<NodeCoupling> nodeCouplingBuffer;
[[vk::binding(2)]] StructuredBuffer<MaterialNode> materialNodeBuffer;
[[vk::binding(3)]] ConstantBuffer<SimPlayback> playback;

[[vk::binding(4)]] StructuredBuffer<float> tempRead;
[[vk::binding(5)]] RWStructuredBuffer<float> tempWrite;

[[vk::binding(8)]] StructuredBuffer<BoundaryNode> boundaryNodeBuffer;
[[vk::binding(9)]] StructuredBuffer<BoundaryState> boundaryStateBuffer;
[[vk::binding(10)]] StructuredBuffer<BoundaryContribution> boundaryContributionBuffer;
[[vk::binding(11)]] StructuredBuffer<float> volumetricPowerDensityBuffer;

[[vk::push_constant]] ConstantBuffer<HeatModelPushConstant> pushConstants;

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint localNodeID = dtid.x;
    if (localNodeID >= pushConstants.elementCount) {
        return;
    }

    BoundaryNode boundaryNode = boundaryNodeBuffer[localNodeID];
    if (boundaryNode.dirichletStateIndex != 0xffffffffu) {
        BoundaryState boundary = boundaryStateBuffer[boundaryNode.dirichletStateIndex];
        tempWrite[localNodeID] = boundary.temperatureC;
        return;
    }

    MaterialNode materialNode = materialNodeBuffer[localNodeID];
    const float metersPerUnit = pushConstants.metersPerUnit;
    const float areaScale = metersPerUnit * metersPerUnit;
    const float volumeScale = areaScale * metersPerUnit;

    Node node = simNodeBuffer[localNodeID];
    float thermalMass = max(materialNode.thermalMass, 1e-12);
    float currentTemp = tempRead[localNodeID];

    float totalConductance = 0.0;
    float totalFlux = 0.0;

    for (uint i = 0; i < node.neighborCount; ++i) {
        NodeCoupling coupling = nodeCouplingBuffer[node.neighborOffset + i];
        float g = coupling.conductance * metersPerUnit;
        uint neighborLocalID = coupling.neighborNodeId;
        float neighborTemp = tempRead[neighborLocalID];
        totalConductance += g;
        totalFlux += g * neighborTemp;
    }

    float dt = playback.deltaTime;
    float kappa = materialNode.conductivityPerMass;

    float invMass = 1.0 / thermalMass;
    float denominator = 1.0;
    float numerator = currentTemp;

    numerator += dt * kappa * totalFlux;
    denominator += dt * kappa * totalConductance;

    float volumetricPower = volumetricPowerDensityBuffer[localNodeID] * node.volume * volumeScale;
    numerator += dt * volumetricPower * invMass;

    for (uint contributionIndex = 0; contributionIndex < boundaryNode.contributionCount; ++contributionIndex) {
        BoundaryContribution contribution = boundaryContributionBuffer[
            boundaryNode.contributionOffset + contributionIndex];
        BoundaryState boundary = boundaryStateBuffer[contribution.stateIndex];
        if (boundary.conditionType == 2u) {
            float boundaryPower = boundary.heatFlux * contribution.area * areaScale;
            numerator += dt * boundaryPower * invMass;
        }
        if (boundary.conditionType == 3u) {
            float boundaryConductance = boundary.heatTransferCoefficient * contribution.area * areaScale;
            numerator += dt * boundaryConductance * boundary.temperatureC * invMass;
            denominator += dt * boundaryConductance * invMass;
        }
    }
    tempWrite[localNodeID] = numerator / denominator;
}
