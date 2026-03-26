#pragma once

struct VoronoiParams {
    float cellSize = 0.005f;
    int voxelResolution = 128;
};

inline bool operator==(const VoronoiParams& lhs, const VoronoiParams& rhs) noexcept {
    return lhs.cellSize == rhs.cellSize &&
        lhs.voxelResolution == rhs.voxelResolution;
}

inline bool operator!=(const VoronoiParams& lhs, const VoronoiParams& rhs) noexcept {
    return !(lhs == rhs);
}
