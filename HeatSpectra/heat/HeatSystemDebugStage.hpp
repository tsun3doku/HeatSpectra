#pragma once

#include <cstdint>

#include "HeatSystemStageContext.hpp"

class HeatSystemDebugStage {
public:
    explicit HeatSystemDebugStage(const HeatSystemStageContext& stageContext);

    void exportDebugArtifacts(bool debugEnable, uint32_t voronoiNodeCount, void* mappedDebugCellGeometryData, void* mappedVoronoiNodeData, void* mappedVoronoiDumpData);
    void exportDebugCellsToOBJ(bool debugEnable, uint32_t voronoiNodeCount, void* mappedDebugCellGeometryData);
    void exportCellVolumes(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData);
    void exportVoronoiDumpInfo(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData, void* mappedVoronoiDumpData);

private:
    HeatSystemStageContext context;
};
