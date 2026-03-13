#include "VoronoiSeeder.hpp"
#include "scene/Model.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <fstream>
#include <omp.h>
#include <unordered_set>
#include <glm/gtx/norm.hpp>

VoronoiSeeder::VoronoiSeeder() {
}

VoronoiSeeder::~VoronoiSeeder() {
}

void VoronoiSeeder::generateSeeds(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh, const Model& volumeMesh, float targetCellSize, VoxelGrid& voxelGrid, int voxelResolution) {
    seeds.clear();
    cellSize = targetCellSize;

    if (intrinsicMesh.vertices.empty() || intrinsicMesh.triangles.empty()) {
        std::cout << "[VoronoiSeeder] Empty intrinsic mesh" << std::endl;
        return;
    }

    std::cout << "  Cell Size: " << cellSize << std::endl;

    // Calculate bounding box
    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);

    // Include volume mesh
    const auto& vertices = volumeMesh.getVertices();
    for (const auto& vertex : vertices) {
        minBounds = glm::min(minBounds, vertex.pos);
        maxBounds = glm::max(maxBounds, vertex.pos);
    }
    // Padding to allow ghost seeds beyond mesh bounds
    float padding = cellSize * 3.0f;

    gridMin = minBounds - glm::vec3(padding);
    gridMax = maxBounds + glm::vec3(padding);

    glm::vec3 gridSize = gridMax - gridMin;
    gridDim.x = std::max(2, static_cast<int>(std::ceil(gridSize.x / cellSize)));
    gridDim.y = std::max(2, static_cast<int>(std::ceil(gridSize.y / cellSize)));
    gridDim.z = std::max(2, static_cast<int>(std::ceil(gridSize.z / cellSize)));

    gridMax = gridMin + glm::vec3(gridDim) * cellSize;

    generateSurfaceSeeds(intrinsicMesh);

    TriangleHashGrid sharedTriGrid;
    sharedTriGrid.build(volumeMesh, gridMin, gridMax, cellSize);

    buildSDFGrid(volumeMesh, sharedTriGrid);

    // Build voxel grid for in/out classification (owned by HeatSystem)
    voxelGrid.build(volumeMesh, sharedTriGrid, voxelResolution);
    const VoxelGrid* voxelGridRef = (voxelGrid.getGridSize() > 0) ? &voxelGrid : nullptr;
    
    // Only generate seeds within this distance
    float maxDistFromSurface = cellSize * 1.5f;  
    generateBlueNoiseSeeds(maxDistFromSurface, voxelGridRef);
}


void VoronoiSeeder::exportSeedsToOBJ(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[VoronoiSeeder] Failed to open file for writing: " << filename << std::endl;
        return;
    }
    
    file << "# VoronoiSeeder Seeds Export\n";
    file << "# Total Seeds: " << seeds.size() << "\n";
    
    // Write vertices
    for (const auto& seed : seeds) {
        file << "v " << seed.pos.x << " " << seed.pos.y << " " << seed.pos.z;
        if (seed.isSurface) {
            file << " 1.0 0.0 0.0"; // Red for surface
        } else {
        file << " 0.0 0.0 1.0";     // Blue for interior
        }
        file << "\n";
    }
    
    file.close();
    std::cout << "[VoronoiSeeder] Exported seeds to: " << filename << std::endl;
}

float VoronoiSeeder::computePointToMeshDistance(const glm::vec3& point, const Model& model) const {
    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();

    float minDistSq = FLT_MAX;

    // Get nearby triangles from spatial hash
    std::vector<size_t> nearbyTriangles;
    triHashGrid.getNearbyTriangles(point, nearbyTriangles);
    if (nearbyTriangles.empty()) {
        const int fallbackRadius = 4;
        triHashGrid.getNearbyTriangles(point, fallbackRadius, nearbyTriangles);
    }

    // Check only nearby triangles
    for (size_t triIdx : nearbyTriangles) {
        const size_t indexBase = triIdx * 3;
        if (indexBase + 2 >= indices.size()) {
            continue;
        }

        glm::vec3 v0 = vertices[indices[indexBase]].pos;
        glm::vec3 v1 = vertices[indices[indexBase + 1]].pos;
        glm::vec3 v2 = vertices[indices[indexBase + 2]].pos;

        glm::vec3 closestPoint = closestPointOnTriangle(point, v0, v1, v2);
        float distSq = glm::distance2(point, closestPoint);

        minDistSq = std::min(minDistSq, distSq);
    }

    return std::sqrt(minDistSq);
}

void VoronoiSeeder::buildSDFGrid(const Model& volumeMesh, const TriangleHashGrid& triangleGrid) {
    sdfGridDim = gridDim;
    sdfCellSize = cellSize;

    size_t totalCells = static_cast<size_t>(sdfGridDim.x) * sdfGridDim.y * sdfGridDim.z;
    sdfGrid.resize(totalCells);

    triHashGrid = triangleGrid;

    // Calculate SDF value at each grid vertex
    #pragma omp parallel for
    for (int z = 0; z < sdfGridDim.z; z++) {
        for (int y = 0; y < sdfGridDim.y; y++) {
            for (int x = 0; x < sdfGridDim.x; x++) {
                glm::vec3 gridPoint = gridMin + glm::vec3(x, y, z) * sdfCellSize;  

                float unsignedDist = computePointToMeshDistance(gridPoint, volumeMesh);

                // Use sdfGridDim for indexing
                size_t idx = z * sdfGridDim.y * sdfGridDim.x + y * sdfGridDim.x + x;
                sdfGrid[idx] = unsignedDist;
            }
        }
    }
}

float VoronoiSeeder::sampleSDFGrid(const glm::vec3& pos) const {
    // Convert world position to grid space
    glm::vec3 gridPos = (pos - gridMin) / sdfCellSize;

    // Clamp to grid range
    gridPos.x = glm::clamp(gridPos.x, 0.0f, static_cast<float>(sdfGridDim.x - 1));
    gridPos.y = glm::clamp(gridPos.y, 0.0f, static_cast<float>(sdfGridDim.y - 1));
    gridPos.z = glm::clamp(gridPos.z, 0.0f, static_cast<float>(sdfGridDim.z - 1));

    int x0 = static_cast<int>(std::floor(gridPos.x));
    int y0 = static_cast<int>(std::floor(gridPos.y));
    int z0 = static_cast<int>(std::floor(gridPos.z));
    int x1 = std::min(x0 + 1, sdfGridDim.x - 1);
    int y1 = std::min(y0 + 1, sdfGridDim.y - 1);
    int z1 = std::min(z0 + 1, sdfGridDim.z - 1);

    float fx = gridPos.x - x0;
    float fy = gridPos.y - y0;
    float fz = gridPos.z - z0;

    // Trilinear interpolation 
    float c000 = sdfGrid[z0 * sdfGridDim.y * sdfGridDim.x + y0 * sdfGridDim.x + x0];
    float c100 = sdfGrid[z0 * sdfGridDim.y * sdfGridDim.x + y0 * sdfGridDim.x + x1];
    float c010 = sdfGrid[z0 * sdfGridDim.y * sdfGridDim.x + y1 * sdfGridDim.x + x0];
    float c110 = sdfGrid[z0 * sdfGridDim.y * sdfGridDim.x + y1 * sdfGridDim.x + x1];
    float c001 = sdfGrid[z1 * sdfGridDim.y * sdfGridDim.x + y0 * sdfGridDim.x + x0];
    float c101 = sdfGrid[z1 * sdfGridDim.y * sdfGridDim.x + y0 * sdfGridDim.x + x1];
    float c011 = sdfGrid[z1 * sdfGridDim.y * sdfGridDim.x + y1 * sdfGridDim.x + x0];
    float c111 = sdfGrid[z1 * sdfGridDim.y * sdfGridDim.x + y1 * sdfGridDim.x + x1];

    float c00 = c000 * (1 - fx) + c100 * fx;
    float c01 = c001 * (1 - fx) + c101 * fx;
    float c10 = c010 * (1 - fx) + c110 * fx;
    float c11 = c011 * (1 - fx) + c111 * fx;

    float c0 = c00 * (1 - fy) + c10 * fy;
    float c1 = c01 * (1 - fy) + c11 * fy;

    return c0 * (1 - fz) + c1 * fz;
}

void VoronoiSeeder::generateSurfaceSeeds(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) {
    for (const auto& vertex : intrinsicMesh.vertices) {
        seeds.push_back(Seed(vertex.position, true));
    }
}

size_t VoronoiSeeder::computeSpatialHash(const glm::vec3& pos, float hashCellSize) const {
    int hx = static_cast<int>(std::floor((pos.x - gridMin.x) / hashCellSize));
    int hy = static_cast<int>(std::floor((pos.y - gridMin.y) / hashCellSize));
    int hz = static_cast<int>(std::floor((pos.z - gridMin.z) / hashCellSize));
    return (size_t(hx) * 73856093) ^ (size_t(hy) * 19349663) ^ (size_t(hz) * 83492791);
}

bool VoronoiSeeder::isTooCloseToExisting(const glm::vec3& candidatePos, float poissonRadius, float hashCellSize,
                                          const std::unordered_map<size_t, std::vector<glm::vec3>>& spatialHash) const {
    float poissonRadiusSq = poissonRadius * poissonRadius;
    
    int hx = static_cast<int>(std::floor((candidatePos.x - gridMin.x) / hashCellSize));
    int hy = static_cast<int>(std::floor((candidatePos.y - gridMin.y) / hashCellSize));
    int hz = static_cast<int>(std::floor((candidatePos.z - gridMin.z) / hashCellSize));
    
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                size_t neighborHash = (size_t(hx + dx) * 73856093) ^ (size_t(hy + dy) * 19349663) ^ (size_t(hz + dz) * 83492791);
                auto it = spatialHash.find(neighborHash);
                if (it != spatialHash.end()) {
                    for (const auto& existingPos : it->second) {
                        float distSq = glm::distance2(candidatePos, existingPos);
                        if (distSq < poissonRadiusSq) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

void VoronoiSeeder::generateBlueNoiseSeeds(float maxDistFromSurface, const VoxelGrid* voxelGrid) {
    std::default_random_engine generator(42);
    std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);
    
    float basePoissonRadius = cellSize * 0.8f;
    int maxAttempts = 30;
    
    float hashCellSize = basePoissonRadius;
    std::unordered_map<size_t, std::vector<glm::vec3>> spatialHash;
    spatialHash.reserve(seeds.size() * 2);
    
    for (const auto& seed : seeds) {
        if (seed.isSurface) {
            size_t hash = computeSpatialHash(seed.pos, hashCellSize);
            spatialHash[hash].push_back(seed.pos);
        }
    }
    
    for (int z = 0; z < gridDim.z; z++) {
        for (int y = 0; y < gridDim.y; y++) {
            for (int x = 0; x < gridDim.x; x++) {
                glm::vec3 cellCenter = gridMin + glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) * cellSize;
                
                bool seedPlaced = false;
                for (int attempt = 0; attempt < maxAttempts && !seedPlaced; attempt++) {
                    glm::vec3 candidatePos = cellCenter + glm::vec3(
                        (uniform01(generator) - 0.5f) * cellSize,
                        (uniform01(generator) - 0.5f) * cellSize,
                        (uniform01(generator) - 0.5f) * cellSize
                    );
                    
                    float distToSurface = sampleSDFGrid(candidatePos);

                    bool isInside = false;
                    if (voxelGrid) {
                        glm::ivec3 voxel = voxelGrid->worldToVoxel(candidatePos);
                        uint8_t occ = voxelGrid->getOccupancy(voxel.x, voxel.y, voxel.z);
                        isInside = (occ == 2 || occ == 1);
                    }

                    bool isGhost = (!isInside && distToSurface > maxDistFromSurface);
     
                    if (!isTooCloseToExisting(candidatePos, basePoissonRadius, hashCellSize, spatialHash)) {
                        seeds.push_back(Seed(candidatePos, false, isGhost));
                        
                        size_t hash = computeSpatialHash(candidatePos, hashCellSize);
                        spatialHash[hash].push_back(candidatePos);
                        
                        seedPlaced = true;
                    }
                }
                
            }
        }
    }
    
}
