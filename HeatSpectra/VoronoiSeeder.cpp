#include "VoronoiSeeder.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <fstream>
#include <omp.h>
#include <map>
#include <set>
#include <unordered_set>
#include <queue>
#include <glm/gtx/norm.hpp>

VoronoiSeeder::VoronoiSeeder() {
}

VoronoiSeeder::~VoronoiSeeder() {
}

void VoronoiSeeder::generateSeeds(const std::vector<CommonSubdivision::IntrinsicTriangle>& intrinsicTriangles, const std::vector<Vertex>& commonVertices, const Model& volumeMesh, float targetCellSize) {
    seeds.clear();
    cellSize = targetCellSize;

    if (intrinsicTriangles.empty() || commonVertices.empty()) {
        std::cout << "[VoronoiSeeder] No intrinsic triangles or vertices provided for seed generation." << std::endl;
        return;
    }

    std::cout << "[VoronoiSeeder] Generating Seeds ..." << std::endl;
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
    // Include surface seeds
    for (const auto& vertex : commonVertices) {
        minBounds = glm::min(minBounds, vertex.pos);
        maxBounds = glm::max(maxBounds, vertex.pos);
    }

    // Extended padding to allow ghost seeds beyond mesh bounds
    float padding = cellSize * 3.0f;

    gridMin = minBounds - glm::vec3(padding);
    gridMax = maxBounds + glm::vec3(padding);

    glm::vec3 gridSize = gridMax - gridMin;
    gridDim.x = std::max(2, static_cast<int>(std::ceil(gridSize.x / cellSize)));
    gridDim.y = std::max(2, static_cast<int>(std::ceil(gridSize.y / cellSize)));
    gridDim.z = std::max(2, static_cast<int>(std::ceil(gridSize.z / cellSize)));

    gridMax = gridMin + glm::vec3(gridDim) * cellSize;

    std::cout << "  Grid Dimensions: " << gridDim.x << " x " << gridDim.y << " x " << gridDim.z << std::endl;

    generateSurfaceSeeds(commonVertices, intrinsicTriangles);

    // Build a single triangle hash grid and reuse it.
    TriangleHashGrid sharedTriGrid;
    sharedTriGrid.build(volumeMesh, gridMin, gridMax, cellSize);

    // Build unsigned distance field for filtering
    buildSDFGrid(volumeMesh, sharedTriGrid);

    // Build voxel grid for inside/out classification (one-time per seed generation)
    const int voxelResolution = 64;
    voxelGrid.build(volumeMesh, sharedTriGrid, voxelResolution);
    voxelGridBuilt = (voxelGrid.getGridSize() > 0);
    
    // Only generate seeds within this distance
    float maxDistFromSurface = cellSize * 1.5f;  
    generateBlueNoiseSeeds(maxDistFromSurface);

    std::cout << "[VoronoiSeeder] Total seeds: " << seeds.size() << std::endl;
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
        glm::vec3 v0 = vertices[indices[triIdx]].pos;
        glm::vec3 v1 = vertices[indices[triIdx + 1]].pos;
        glm::vec3 v2 = vertices[indices[triIdx + 2]].pos;

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

    std::cout << "  SDF Grid: " << sdfGridDim.x << "x" << sdfGridDim.y << "x" << sdfGridDim.z << std::endl;

    triHashGrid = triangleGrid;

    // Calculate SDF value at each grid vertex
    #pragma omp parallel for
    for (int z = 0; z < sdfGridDim.z; z++) {
        for (int y = 0; y < sdfGridDim.y; y++) {
            for (int x = 0; x < sdfGridDim.x; x++) {
                glm::vec3 gridPoint = gridMin + glm::vec3(x, y, z) * sdfCellSize;  

                float unsignedDist = computePointToMeshDistance(gridPoint, volumeMesh);

                // Use sdfGridDim for indexing (unsigned distance only)
                size_t idx = z * sdfGridDim.y * sdfGridDim.x + y * sdfGridDim.x + x;
                sdfGrid[idx] = unsignedDist;
            }
        }
    }

    std::cout << "  SDF grid built (" << totalCells << " cells)." << std::endl;
}

float VoronoiSeeder::sampleSDFGrid(const glm::vec3& pos) const {
    // Convert world position to SDF grid space
    glm::vec3 gridPos = (pos - gridMin) / sdfCellSize;

    // Clamp to SDF grid range
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

void VoronoiSeeder::generateSurfaceSeeds(const std::vector<Vertex>& commonVertices, const std::vector<CommonSubdivision::IntrinsicTriangle>& intrinsicTriangles) {
    std::map<uint32_t, glm::vec3> intrinsicVertexPositions;
    
    for (const auto& tri : intrinsicTriangles) {
        for (size_t k = 0; k < 3; k++) {
            uint32_t intrinsicID = tri.intrinsicVertices[k];
            uint32_t commonIdx = tri.indices[tri.cornerIndices[k]];
            intrinsicVertexPositions[intrinsicID] = commonVertices[commonIdx].pos;
        }
    }
    
    for (const auto& [id, pos] : intrinsicVertexPositions) {
        seeds.push_back(Seed(pos, true));
    }
    
    std::cout << "  Generated " << seeds.size() << " surface seeds." << std::endl;
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

void VoronoiSeeder::generateBlueNoiseSeeds(float maxDistFromSurface) {
    std::default_random_engine generator(42);
    std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);
    
    float basePoissonRadius = cellSize * 0.8f;
    int maxAttempts = 30;
    
    int seedCount = 0;
    int skippedCount = 0;
    int sdfSkippedCount = 0; 
    
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
                    if (voxelGridBuilt) {
                        glm::ivec3 voxel = voxelGrid.worldToVoxel(candidatePos);
                        uint8_t occ = voxelGrid.getOccupancy(voxel.x, voxel.y, voxel.z);
                        isInside = (occ == 2 || occ == 1);
                    }

                    // Ghosts are outside the mesh beyond the padding distance.
                    bool isGhost = (!isInside && distToSurface > maxDistFromSurface);

                    
                    if (!isTooCloseToExisting(candidatePos, basePoissonRadius, hashCellSize, spatialHash)) {
                        seeds.push_back(Seed(candidatePos, false, isGhost));
                        
                        size_t hash = computeSpatialHash(candidatePos, hashCellSize);
                        spatialHash[hash].push_back(candidatePos);
                        
                        if (isGhost) {
                            sdfSkippedCount++;
                        } else {
                            seedCount++;
                        }
                        seedPlaced = true;
                    }
                }
                
                if (!seedPlaced) {
                    skippedCount++;
                }
            }
        }
    }
    
    std::cout << "  Generated " << seedCount << " regular seeds + " << sdfSkippedCount << " ghost seeds (max dist: " << maxDistFromSurface << ")" << std::endl;
    std::cout << "  Skipped " << skippedCount << " cells (insufficient spacing)" << std::endl;
}
