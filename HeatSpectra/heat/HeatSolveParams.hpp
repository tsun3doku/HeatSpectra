#pragma once

struct HeatSolveParams {
    float cellSize = 0.005f;
    int voxelResolution = 128;

    bool operator==(const HeatSolveParams& rhs) const {
        return cellSize == rhs.cellSize &&
            voxelResolution == rhs.voxelResolution;
    }

    bool operator!=(const HeatSolveParams& rhs) const {
        return !(*this == rhs);
    }
};
