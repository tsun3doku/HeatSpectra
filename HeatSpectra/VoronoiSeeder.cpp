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

    // Build SDF for filtering
    buildSDFGrid(volumeMesh);  
    
    // Only generate seeds within this distance
    float maxDistFromSurface = cellSize * 1.5f;  
    generateUniformBlueNoiseSeeds(maxDistFromSurface);

    std::cout << "[VoronoiSeeder] Total seeds: " << seeds.size() << std::endl;
}

void VoronoiSeeder::buildSpatialIndex() {
    if (seeds.empty())
        return;

    std::cout << "[VoronoiSeeder] Building Spatial Index..." << std::endl;

    // Assign each seed to a grid cell
    std::vector<std::pair<uint32_t, uint32_t>> seedCellPairs;
    seedCellPairs.reserve(seeds.size());

    for (size_t i = 0; i < seeds.size(); i++) {
        glm::ivec3 gridCoord = worldToGrid(seeds[i].pos);
        if (isValidGridCoord(gridCoord.x, gridCoord.y, gridCoord.z)) {
            uint32_t cellIdx = static_cast<uint32_t>(getVoxelIndex(gridCoord.x, gridCoord.y, gridCoord.z));
            seedCellPairs.push_back({ cellIdx, static_cast<uint32_t>(i) });
        }
        else {
            std::cerr << "Warning: Seed outside grid bounds" << std::endl;
        }
    }

    // Sort seeds by cell index
    std::sort(seedCellPairs.begin(), seedCellPairs.end());

    // Reorder seeds vector to match sorted order
    std::vector<Seed> sortedSeeds;
    sortedSeeds.reserve(seeds.size());
    for (const auto& pair : seedCellPairs) {
        sortedSeeds.push_back(seeds[pair.second]);
    }
    seeds = sortedSeeds;

    // Build the grid index
    size_t totalCells = gridDim.x * gridDim.y * gridDim.z;
    spatialIndex.clear();
    spatialIndex.resize(totalCells, { 0, 0 });

    if (seedCellPairs.empty())
        return;

    uint32_t currentCell = seedCellPairs[0].first;
    uint32_t startIdx = 0;
    uint32_t count = 0;

    for (size_t i = 0; i < seedCellPairs.size(); i++) {
        uint32_t cellIdx = seedCellPairs[i].first;

        if (cellIdx != currentCell) {
            // Finish previous cell
            spatialIndex[currentCell].offset = startIdx;
            spatialIndex[currentCell].count = count;

            // Start new cell
            currentCell = cellIdx;
            startIdx = static_cast<uint32_t>(i);
            count = 0;
        }
        count++;
    }
    // Finish last cell
    spatialIndex[currentCell].offset = startIdx;
    spatialIndex[currentCell].count = count;

    std::cout << "  Spatial Index built. Sorted " << seeds.size() << " seeds into " << totalCells << " grid cells." << std::endl;
}

float VoronoiSeeder::computeWindingNumber(const glm::vec3& point, const Model& model) const {
    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();
    float totalSolidAngle = 0.0f;

    // Only check nearby triangles using spatial hash
    std::vector<size_t> nearbyTriangles;
    triHashGrid.getNearbyTriangles(point, nearbyTriangles);
    
    // If no nearby triangles, assume outside
    if (nearbyTriangles.empty()) {
        return 0.0f;
    }

    for (size_t triIdx : nearbyTriangles) {
        glm::vec3 a = vertices[indices[triIdx]].pos - point;
        glm::vec3 b = vertices[indices[triIdx + 1]].pos - point;
        glm::vec3 c = vertices[indices[triIdx + 2]].pos - point;

        float al = glm::length(a);
        float bl = glm::length(b);
        float cl = glm::length(c);
        float num = glm::determinant(glm::mat3(a, b, c));
        float den = al * bl * cl + glm::dot(a, b) * cl + glm::dot(a, c) * bl + glm::dot(b, c) * al;

        totalSolidAngle += 2.0f * std::atan2(num, den);
    }

    return totalSolidAngle / (4.0f * glm::pi<float>());
}

glm::ivec3 VoronoiSeeder::worldToGrid(const glm::vec3& worldPos) const {
    glm::vec3 gridPos = (worldPos - gridMin) / cellSize;
    glm::ivec3 coord(
        static_cast<int>(std::floor(gridPos.x)),
        static_cast<int>(std::floor(gridPos.y)),
        static_cast<int>(std::floor(gridPos.z))
    );

    coord.x = std::max(0, std::min(coord.x, gridDim.x - 1));
    coord.y = std::max(0, std::min(coord.y, gridDim.y - 1));
    coord.z = std::max(0, std::min(coord.z, gridDim.z - 1));

    return coord;
}

glm::vec3 VoronoiSeeder::getVoxelCenter(int x, int y, int z) const {
    return gridMin + glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) * cellSize;
}

size_t VoronoiSeeder::getVoxelIndex(int x, int y, int z) const {
    return z * gridDim.y * gridDim.x + y * gridDim.x + x;
}

bool VoronoiSeeder::isPointInsideMesh(const glm::vec3& point, const Model& model) const {
    return std::abs(computeWindingNumber(point, model)) > 0.5f;
}

bool VoronoiSeeder::isValidGridCoord(int x, int y, int z) const {
    return x >= 0 && x < gridDim.x &&
        y >= 0 && y < gridDim.y &&
        z >= 0 && z < gridDim.z;
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

glm::vec3 VoronoiSeeder::closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) const {
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;

    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);

    if (d1 <= 0.0f && d2 <= 0.0f) 
        return a;  // Vertex region A

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);

    if (d3 >= 0.0f && d4 <= d3) 
        return b;   // Vertex region B

    float vc = d1 * d4 - d3 * d2;

    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab;  // Edge AB
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);

    if (d6 >= 0.0f && d5 <= d6) 
        return c;  // Vertex region C

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac;  // Edge AC
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);  // Edge BC
    }

    // Inside face region
    float denom = 1.0f / (va + vb + vc);
    float v = vb *denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

float VoronoiSeeder::computePointToMeshDistance(const glm::vec3& point, const Model& model) const {
    const auto& vertices = model.getVertices();
    const auto& indices = model.getIndices();

    float minDistSq = FLT_MAX;

    // Get nearby triangles from spatial hash
    std::vector<size_t> nearbyTriangles;
    triHashGrid.getNearbyTriangles(point, nearbyTriangles);

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

void VoronoiSeeder::buildSDFGrid(const Model& volumeMesh) {
    sdfGridDim = gridDim;
    sdfCellSize = cellSize;

    size_t totalCells = static_cast<size_t>(sdfGridDim.x) * sdfGridDim.y * sdfGridDim.z;
    sdfGrid.resize(totalCells);

    std::cout << "  SDF Grid: " << sdfGridDim.x << "x" << sdfGridDim.y << "x" << sdfGridDim.z << std::endl;

    triHashGrid.build(volumeMesh, gridMin, gridMax, cellSize * 2.0f);

    // Calculate SDF value at each grid vertex
    #pragma omp parallel for
    for (int z = 0; z < sdfGridDim.z; z++) {
        for (int y = 0; y < sdfGridDim.y; y++) {
            for (int x = 0; x < sdfGridDim.x; x++) {
                glm::vec3 gridPoint = gridMin + glm::vec3(x, y, z) * sdfCellSize;  

                float unsignedDist = computePointToMeshDistance(gridPoint, volumeMesh);
                float windingNum = computeWindingNumber(gridPoint, volumeMesh);
                
                // Winding: positive = inside, negative = outside
                // SDF: negative = inside, positive = outside
                float sign;
                if (windingNum > 0.5f) {
                    sign = -1.0f;  // Inside
                } else {
                    sign = 1.0f;   // Outside
                }

                // Use sdfGridDim for indexing
                size_t idx = z * sdfGridDim.y * sdfGridDim.x + y * sdfGridDim.x + x;
                sdfGrid[idx] = sign * unsignedDist;
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

void VoronoiSeeder::generateUniformBlueNoiseSeeds(float maxDistFromSurface) {
    std::default_random_engine generator(42);
    std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);
    
    // Poisson disk sampling with SDF filtering
    // Only generate seeds within maxDistFromSurface of the mesh surface
    float basePoissonRadius = cellSize * 0.8f;
    int maxAttempts = 30;
    
    int seedCount = 0;
    int skippedCount = 0;
    int sdfSkippedCount = 0; 
    
    // SPATIAL HASH GRID for O(1) collision detection
    // Grid cell size = Poisson radius (only need to check immediate neighbors)
    float hashCellSize = basePoissonRadius;
    auto hashFunc = [&](const glm::vec3& pos) -> size_t {
        int hx = static_cast<int>(std::floor((pos.x - gridMin.x) / hashCellSize));
        int hy = static_cast<int>(std::floor((pos.y - gridMin.y) / hashCellSize));
        int hz = static_cast<int>(std::floor((pos.z - gridMin.z) / hashCellSize));
        // Simple hash combining x, y, z
        return (size_t(hx) * 73856093) ^ (size_t(hy) * 19349663) ^ (size_t(hz) * 83492791);
    };
    
    std::unordered_map<size_t, std::vector<glm::vec3>> spatialHash;
    spatialHash.reserve(seeds.size() * 2);
    
    // Add existing surface seeds to spatial hash
    for (const auto& seed : seeds) {
        if (seed.isSurface) {
            size_t hash = hashFunc(seed.pos);
            spatialHash[hash].push_back(seed.pos);
        }
    }
    
    // Lambda to check if point is too close to existing seeds
    auto isTooClose = [&](const glm::vec3& candidatePos) -> bool {
        float poissonRadiusSq = basePoissonRadius * basePoissonRadius;
        
        // Check 3x3x3 neighborhood in hash grid
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
    };
    
    // Sample entire grid uniformly 
    for (int z = 0; z < gridDim.z; z++) {
        for (int y = 0; y < gridDim.y; y++) {
            for (int x = 0; x < gridDim.x; x++) {
                glm::vec3 cellCenter = gridMin + glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) * cellSize;
                
                bool seedPlaced = false;
                for (int attempt = 0; attempt < maxAttempts && !seedPlaced; attempt++) {
                    // Generate random position within cell
                    glm::vec3 candidatePos = cellCenter + glm::vec3(
                        (uniform01(generator) - 0.5f) * cellSize,
                        (uniform01(generator) - 0.5f) * cellSize,
                        (uniform01(generator) - 0.5f) * cellSize
                    );
                    
                    // Check SDF distance to determine if this should be a ghost seed
                    float sdfValue = sampleSDFGrid(candidatePos);
                    bool isGhost = std::abs(sdfValue) > maxDistFromSurface;
                    
                    // CRITICAL: Track whether seed is inside mesh (SDF ≤ 0)
                    // Interior seeds should have non-zero volume after restriction
                    // Exterior seeds often get completely clipped → zero volume
                    bool isInside = (sdfValue <= 0.0f);
                    
                    // Spatial hash collision check (O(1) instead of O(n))
                    if (!isTooClose(candidatePos)) {
                        // Accept this seed with isInside flag
                        seeds.push_back(Seed(candidatePos, false, false, isGhost, isInside));
                        
                        // Add to spatial hash
                        size_t hash = hashFunc(candidatePos);
                        spatialHash[hash].push_back(candidatePos);
                        
                        if (isGhost) {
                            sdfSkippedCount++;  // Count ghost seeds
                        } else {
                            seedCount++;        // Count regular seeds
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

