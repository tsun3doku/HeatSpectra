#include "HeatSystemDebugStage.hpp"

#include "HeatSystem.hpp"

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

    DebugCellGeometry* cells = static_cast<DebugCellGeometry*>(mappedDebugCellGeometryData);

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

    std::cout << "[HeatSystem] Writing OBJ file (checking " << voronoiNodeCount << " cells)..." << std::endl;

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
    std::cout << "Exported " << exportCount << " cells to: voronoi_unrestricted_debug_cells.obj\n";
}

void HeatSystemDebugStage::exportCellVolumes(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData) {
    if (!debugEnable) {
        return;
    }

    std::cout << "[HeatSystem] Exporting cell volumes..." << std::endl;

    if (!mappedVoronoiNodeData) {
        std::cerr << "[HeatSystem] Error: VoronoiNode buffer not mapped" << std::endl;
        return;
    }

    VoronoiNodeGPU* nodes = static_cast<VoronoiNodeGPU*>(mappedVoronoiNodeData);

    std::ofstream volumeFile("cell_volumes.txt");
    volumeFile << "# Cell Index -> Restricted Volume\n";
    volumeFile << "# NOTE: Using GPU filtered seed positions (cells.size() = " << voronoiNodeCount << ")\n";
    for (uint32_t i = 0; i < voronoiNodeCount; i++) {
        volumeFile << "Cell " << i << " -> Volume: " << nodes[i].volume << "\n";
    }

    volumeFile.close();
    std::cout << "[HeatSystem] Exported " << voronoiNodeCount
              << " cell volumes to: cell_volumes.txt" << std::endl;
}

void HeatSystemDebugStage::exportVoronoiDumpInfo(bool debugEnable, uint32_t voronoiNodeCount, void* mappedVoronoiNodeData, void* mappedVoronoiDumpData) {
    if (!debugEnable) {
        return;
    }

    std::cout << "[HeatSystem] Exporting Voronoi debug dump..." << std::endl;

    if (!mappedVoronoiDumpData) {
        std::cerr << "[HeatSystem] Error: VoronoiDumpInfo buffer not mapped" << std::endl;
        return;
    }

    VoronoiDumpInfo* dumpInfos = static_cast<VoronoiDumpInfo*>(mappedVoronoiDumpData);

    double totalRestrictedVolumePos = 0.0;
    double totalRestrictedVolumeNegAbs = 0.0;
    uint32_t negativeVolumeCount = 0;
    if (mappedVoronoiNodeData) {
        const VoronoiNodeGPU* nodes = static_cast<const VoronoiNodeGPU*>(mappedVoronoiNodeData);
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

    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++) {
        dumpInfos[slot].totalMeshVolume = static_cast<float>(totalRestrictedVolumePos);
        dumpInfos[slot].negativeVolumeCellCount = negativeVolumeCount;
        dumpInfos[slot].negativeVolumeSumAbs = static_cast<float>(totalRestrictedVolumeNegAbs);
    }

    std::ofstream dumpFile("voronoi_debug_dump.txt");
    dumpFile << std::scientific << std::setprecision(10);

    dumpFile << "Total Mesh Volume: " << totalRestrictedVolumePos << "\n";
    dumpFile << "Negative volume cells: " << negativeVolumeCount << " Sum =" << totalRestrictedVolumeNegAbs << ")\n";
    dumpFile << "\n";

    for (uint32_t slot = 0; slot < DEBUG_DUMP_CELL_COUNT; slot++) {
        const VoronoiDumpInfo& info = dumpInfos[slot];

        if (info.cellID == 0) {
            continue;
        }

        dumpFile << "CELL " << info.cellID << " (Slot " << slot << ")\n";
        dumpFile << "Seed: (" << info.seedPos.x << ", " << info.seedPos.y << ", " << info.seedPos.z << ")\n";
        dumpFile << "Volumes:\n";
        dumpFile << "  Unrestricted: " << info.unrestrictedVolume << "\n";
        dumpFile << "  Restricted:   " << info.restrictedVolume << "\n";
        dumpFile << "Plane Areas (" << info.planeAreaCount << "):\n";
        for (uint32_t i = 0; i < info.planeAreaCount && i < DEBUG_MAX_PLANE_AREAS; ++i) {
            const DebugPlaneArea& planeArea = info.planeAreas[i];
            dumpFile << "  Plane " << planeArea.planeIndex
                     << " -> Neighbor " << planeArea.neighborCellID
                     << ", Area: " << planeArea.area << "\n";
        }
        dumpFile << "\n";
    }

    dumpFile.close();
    std::cout << "[HeatSystem] Exported debug dump to: voronoi_debug_dump.txt" << std::endl;
}
