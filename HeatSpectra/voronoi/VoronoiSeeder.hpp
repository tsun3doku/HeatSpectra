#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "spatial/TriangleHashGrid.hpp"
#include "spatial/VoxelGrid.hpp"
#include "util/GeometryUtils.hpp"
#include "util/Structs.hpp"

class Model;

class VoronoiSeeder {
public:
    struct Seed {
        glm::vec3 pos;
        bool isSurface;
        bool isGhost;    
        float volume; 
        Seed() : pos(0.0f), isSurface(false), isGhost(false), volume(0.0f) {}
        Seed(glm::vec3 p, bool s, bool g = false) 
            : pos(p), isSurface(s), isGhost(g), volume(0.0f) {}
    };

    VoronoiSeeder();
    ~VoronoiSeeder();
    
    void generateSeeds(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh, const Model& volumeMesh, float targetCellSize, VoxelGrid& voxelGrid, int voxelResolution);
    void exportSeedsToOBJ(const std::string& filename) const;

    const std::vector<Seed>& getSeeds() const { return seeds; }
    
    void updateSeedPosition(size_t index, const glm::vec3& newPos) {
        if (index < seeds.size()) {
            seeds[index].pos = newPos;
        }
    }
    glm::ivec3 getDimensions() const { return gridDim; }
    float getCellSize() const { return cellSize; }
    glm::vec3 getGridMin() const { return gridMin; }
    glm::vec3 getGridMax() const { return gridMax; }

private:
    TriangleHashGrid triHashGrid;
    std::vector<Seed> seeds;
    std::vector<float> sdfGrid;  
    glm::ivec3 sdfGridDim;   
    float sdfCellSize;      
      
    glm::ivec3 gridDim;
    glm::vec3 gridMin;
    glm::vec3 gridMax;
    float cellSize;

    void generateSurfaceSeeds(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh);
    void generateBlueNoiseSeeds(float maxDistFromSurface, const VoxelGrid* voxelGrid);
    
    size_t computeSpatialHash(const glm::vec3& pos, float hashCellSize) const;
    bool isTooCloseToExisting(const glm::vec3& candidatePos, float poissonRadius, float hashCellSize, 
                               const std::unordered_map<size_t, std::vector<glm::vec3>>& spatialHash) const; 

    void buildSDFGrid(const Model& volumeMesh, const TriangleHashGrid& triangleGrid);
    float sampleSDFGrid(const glm::vec3& pos) const;
    float computePointToMeshDistance(const glm::vec3& point, const Model& model) const;
};
