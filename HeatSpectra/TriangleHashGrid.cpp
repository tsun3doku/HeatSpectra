#include "TriangleHashGrid.hpp"
#include "Model.hpp"
#include <iostream>
#include <algorithm>
#include <omp.h>

TriangleHashGrid::TriangleHashGrid()
    : cellSize_(1.0f), gridMin_(0.0f), gridMax_(1.0f), gridDim_(1) {
}

TriangleHashGrid::~TriangleHashGrid() {
}

void TriangleHashGrid::build(const Model& model, const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize) {
    clear();

    gridMin_ = gridMin;
    gridMax_ = gridMax;
    cellSize_ = cellSize;

    glm::vec3 gridSize = gridMax - gridMin;
    gridDim_ = glm::ivec3(
        std::max(1, static_cast<int>(std::ceil(gridSize.x / cellSize))),
        std::max(1, static_cast<int>(std::ceil(gridSize.y / cellSize))),
        std::max(1, static_cast<int>(std::ceil(gridSize.z / cellSize)))
    );

    std::cout << "  Building triangle hash grid: " << gridDim_.x << "x"
        << gridDim_.y << "x" << gridDim_.z << std::endl;

    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();
    
    // Local thread grids to avoid race conditions
    std::vector<std::unordered_map<size_t, std::vector<size_t>>> threadLocalGrids;
    
    #pragma omp parallel
    {
        #pragma omp single
        {
            threadLocalGrids.resize(omp_get_num_threads());
        }
        
        int threadId = omp_get_thread_num();
        
        // Insert each triangle into all cells it overlaps
        #pragma omp for
        for (int i = 0; i < static_cast<int>(indices.size()); i += 3) {
            glm::vec3 v0 = vertices[indices[i]].pos;
            glm::vec3 v1 = vertices[indices[i + 1]].pos;
            glm::vec3 v2 = vertices[indices[i + 2]].pos;

            // Triangle bounding box
            glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
            glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);

            glm::ivec3 cellMin = worldToCell(triMin);
            glm::ivec3 cellMax = worldToCell(triMax);

            // Insert triangle into all overlapping cells
            for (int z = cellMin.z; z <= cellMax.z; z++) {
                for (int y = cellMin.y; y <= cellMax.y; y++) {
                    for (int x = cellMin.x; x <= cellMax.x; x++) {
                        size_t hash = hashCell(x, y, z);
                        threadLocalGrids[threadId][hash].push_back(i);
                    }
                }
            }
        }
    }
    
    // Merge local thread grids into main grid
    for (const auto& localGrid : threadLocalGrids) {
        for (const auto& [hash, triangles] : localGrid) {
            grid_[hash].insert(grid_[hash].end(), triangles.begin(), triangles.end());
        }
    }

    std::cout << "  Triangle hash built: " << grid_.size() << " non-empty cells" << std::endl;
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();

    glm::ivec3 cell = worldToCell(position);

    // Check this cell and neighbors 
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int cx = cell.x + dx;
                int cy = cell.y + dy;
                int cz = cell.z + dz;

                if (cx < 0 || cx >= gridDim_.x ||
                    cy < 0 || cy >= gridDim_.y ||
                    cz < 0 || cz >= gridDim_.z) 
                    continue;

                size_t hash = hashCell(cx, cy, cz);
                auto it = grid_.find(hash);
                if (it != grid_.end()) {
                    outTriangles.insert(outTriangles.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, int radiusCells, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();
    if (radiusCells <= 1) {
        getNearbyTriangles(position, outTriangles);
        return;
    }

    glm::ivec3 cell = worldToCell(position);
    const int r = radiusCells;

    for (int dz = -r; dz <= r; dz++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int cx = cell.x + dx;
                int cy = cell.y + dy;
                int cz = cell.z + dz;

                if (cx < 0 || cx >= gridDim_.x ||
                    cy < 0 || cy >= gridDim_.y ||
                    cz < 0 || cz >= gridDim_.z) 
                    continue;

                size_t hash = hashCell(cx, cy, cz);
                auto it = grid_.find(hash);
                if (it != grid_.end()) {
                    outTriangles.insert(outTriangles.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }
}

void TriangleHashGrid::clear() {
    grid_.clear();
}

size_t TriangleHashGrid::hashCell(int x, int y, int z) const {
    return z * gridDim_.y * gridDim_.x + y * gridDim_.x + x;
}

glm::ivec3 TriangleHashGrid::worldToCell(const glm::vec3& pos) const {
    glm::vec3 gridPos = (pos - gridMin_) / cellSize_;
    return glm::ivec3(
        glm::clamp(static_cast<int>(gridPos.x), 0, gridDim_.x - 1),
        glm::clamp(static_cast<int>(gridPos.y), 0, gridDim_.y - 1),
        glm::clamp(static_cast<int>(gridPos.z), 0, gridDim_.z - 1)
    );
}
