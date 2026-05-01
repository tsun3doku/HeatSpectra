#include "HeatSystemDebugStage.hpp"

#include "HeatSystem.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>

HeatSystemDebugStage::HeatSystemDebugStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemDebugStage::exportDebugArtifacts(bool debugEnable, uint32_t voronoiNodeCount, void* mappedDebugCellGeometryData, 
    void* mappedVoronoiNodeData, void* mappedVoronoiDumpData) {
    exportDebugCellsToOBJ(debugEnable, voronoiNodeCount, mappedDebugCellGeometryData);
    exportCellVolumes(debugEnable, voronoiNodeCount, mappedVoronoiNodeData);
    exportVoronoiDumpInfo(debugEnable, voronoiNodeCount, mappedVoronoiNodeData, mappedVoronoiDumpData);
}

void HeatSystemDebugStage::exportDebugCellsToOBJ(bool debugEnable, uint32_t voronoiNodeCount, void* mappedDebugCellGeometryData) {
    if (!debugEnable) {
        return;
    }

    if (!mappedDebugCellGeometryData) {
        return;
    }

    voronoi::DebugCellGeometry* cells = static_cast<voronoi::DebugCellGeometry*>(mappedDebugCellGeometryData);

    std::ofstream obj("voronoi_unrestricted_debug_cells.obj");
    if (!obj) {
        std::cerr << "[HeatSystem] Failed to create OBJ file!" << std::endl;
        return;
    }

    char buffer[65536];
    obj.rdbuf()->pubsetbuf(buffer, sizeof(buffer));

    obj << "# Unrestricted Voronoi Cells\n";
    obj << "o Voronoi_Cells_Combined\n";
    uint32_t offset = 1;
    uint32_t exportCount = 0;

    for (uint32_t i = 0; i < voronoiNodeCount; i++) {
        if (cells[i].vertexCount == 0) {
            continue;
        }

        for (uint32_t v = 0; v < cells[i].vertexCount; v++) {
            obj << "v " << cells[i].vertices[v].x << " " << cells[i].vertices[v].y << " " << cells[i].vertices[v].z << "\n";
        }
        for (uint32_t t = 0; t < cells[i].triangleCount; t++) {
            obj << "f " << (offset + cells[i].triangles[t].x) << " "
                << (offset + cells[i].triangles[t].y) << " "
                << (offset + cells[i].triangles[t].z) << "\n";
        }
        offset += cells[i].vertexCount;
        exportCount++;
    }

    obj.close();
}

void HeatSystemDebugStage::exportCellVolumes(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData) {
    if (!debugEnable) {
        return;
    }

    if (!mappedVoronoiNodeData) {
        std::cerr << "[HeatSystem] Error: voronoi::Node buffer not mapped" << std::endl;
        return;
    }

    voronoi::Node* nodes = static_cast<voronoi::Node*>(mappedVoronoiNodeData);

    std::ofstream volumeFile("cell_volumes.txt");
    volumeFile << "# Cell Index -> Restricted Volume\n";
    volumeFile << "# NOTE: Using GPU filtered seed positions (cells.size() = " << voronoiNodeCount << ")\n";
    for (uint32_t i = 0; i < voronoiNodeCount; i++) {
        volumeFile << "Cell " << i << " -> Volume: " << nodes[i].volume << "\n";
    }

    volumeFile.close();
}

void HeatSystemDebugStage::exportVoronoiDumpInfo(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData, void* mappedVoronoiDumpData) {
    if (!debugEnable) {
        return;
    }

    if (!mappedVoronoiDumpData) {
        std::cerr << "[HeatSystem] Error: voronoi::DumpInfo buffer not mapped" << std::endl;
        return;
    }

    voronoi::DumpInfo* dumpInfos = static_cast<voronoi::DumpInfo*>(mappedVoronoiDumpData);

    double totalRestrictedVolumePos = 0.0;
    double totalRestrictedVolumeNegAbs = 0.0;
    uint32_t negativeVolumeCount = 0;
    if (mappedVoronoiNodeData) {
        const voronoi::Node* nodes = static_cast<const voronoi::Node*>(mappedVoronoiNodeData);
        for (uint32_t i = 0; i < voronoiNodeCount; i++) {
            const double v = (double)nodes[i].volume;
            if (v > 0.0) {
                totalRestrictedVolumePos += v;
            } else if (v < 0.0) {
                negativeVolumeCount++;
                totalRestrictedVolumeNegAbs += -v;
            }
        }
    }

    for (uint32_t slot = 0; slot < voronoi::DEBUG_DUMP_CELL_COUNT; slot++) {
        dumpInfos[slot].totalMeshVolume = static_cast<float>(totalRestrictedVolumePos);
        dumpInfos[slot].negativeVolumeCellCount = negativeVolumeCount;
        dumpInfos[slot].negativeVolumeSumAbs = static_cast<float>(totalRestrictedVolumeNegAbs);
    }

    std::ofstream dumpFile("voronoi_debug_dump.txt");
    dumpFile << std::scientific << std::setprecision(10);

    dumpFile << "Total Mesh Volume: " << totalRestrictedVolumePos << "\n";
    dumpFile << "Negative volume cells: " << negativeVolumeCount << " Sum =" << totalRestrictedVolumeNegAbs << ")\n";
    dumpFile << "\n";

    for (uint32_t slot = 0; slot < voronoi::DEBUG_DUMP_CELL_COUNT; slot++) {
        const voronoi::DumpInfo& info = dumpInfos[slot];

        if (info.cellID == 0) {
            continue;
        }

        dumpFile << "CELL " << info.cellID << " (Slot " << slot << ")\n";
        dumpFile << "Seed: (" << info.seedPos.x << ", " << info.seedPos.y << ", " << info.seedPos.z << ")\n";
        dumpFile << "Volumes:\n";
        dumpFile << "  Unrestricted: " << info.unrestrictedVolume << "\n";
        dumpFile << "  Restricted:   " << info.restrictedVolume << "\n";
        dumpFile << "Plane Areas (" << info.planeAreaCount << "):\n";
        for (uint32_t i = 0; i < info.planeAreaCount && i < voronoi::DEBUG_MAX_PLANE_AREAS; ++i) {
            const voronoi::DebugPlaneArea& planeArea = info.planeAreas[i];
            dumpFile << "  Plane " << planeArea.planeIndex
                     << " -> Neighbor " << planeArea.neighborCellID
                     << ", Area: " << planeArea.area << "\n";
        }
        dumpFile << "\n";
    }

    dumpFile.close();
}
