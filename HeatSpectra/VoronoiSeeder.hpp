#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "CommonSubdivision.hpp"
#include "TriangleHashGrid.hpp"
#include "Structs.hpp"

class Model;

class VoronoiSeeder {
public:
    struct Seed {
        glm::vec3 pos;
        bool isSurface;
        bool isExterior;  // Exterior boundary seed (outside mesh, for bounding only)
        bool isGhost;     // Ghost seed (far from surface, clips boundary cells but no cell built)
        bool isInside;    // Interior seed (inside mesh, SDF ≤ 0) - only these should be used for surface mapping
        float volume; 
        Seed() : pos(0.0f), isSurface(false), isExterior(false), isGhost(false), isInside(false), volume(0.0f) {}
        Seed(glm::vec3 p, bool s, bool e = false, bool g = false, bool i = false) 
            : pos(p), isSurface(s), isExterior(e), isGhost(g), isInside(i), volume(0.0f) {}
    };

    // Spatial indexing
    struct GridBin {
        uint32_t offset;
        uint32_t count;
    };

    VoronoiSeeder();
    ~VoronoiSeeder();
    
    void generateSeeds(const std::vector<CommonSubdivision::IntrinsicTriangle>& intrinsicTriangles, const std::vector<Vertex>& commonVertices, const Model& volumeMesh, float targetCellSize);
    void buildSpatialIndex();
    float computeWindingNumber(const glm::vec3& point, const Model& model) const;

    glm::ivec3 worldToGrid(const glm::vec3& worldPos) const;
    glm::vec3 getVoxelCenter(int x, int y, int z) const;
    size_t getVoxelIndex(int x, int y, int z) const;
    bool isPointInsideMesh(const glm::vec3& point, const Model& model) const;
    bool isValidGridCoord(int x, int y, int z) const;
    void exportSeedsToOBJ(const std::string& filename) const;

    const std::vector<Seed>& getSeeds() const { return seeds; }
    
    // Update a seed's position (for Lloyd relaxation)
    void updateSeedPosition(size_t index, const glm::vec3& newPos) {
        if (index < seeds.size()) {
            seeds[index].pos = newPos;
        }
    }
    glm::ivec3 getDimensions() const { return gridDim; }
    float getCellSize() const { return cellSize; }
    glm::vec3 getGridMin() const { return gridMin; }
    glm::vec3 getGridMax() const { return gridMax; }
    const std::vector<GridBin>& getSpatialIndex() const { return spatialIndex; }

private:
    TriangleHashGrid triHashGrid;
    std::vector<Seed> seeds;
    std::vector<GridBin> spatialIndex;
    std::vector<float> sdfGrid;  
    glm::ivec3 sdfGridDim;   
    float sdfCellSize;      
      
    glm::ivec3 gridDim;
    glm::vec3 gridMin;
    glm::vec3 gridMax;
    float cellSize;

    void generateSurfaceSeeds(const std::vector<Vertex>& commonVertices, const std::vector<CommonSubdivision::IntrinsicTriangle>& intrinsicTriangles);
    void generateUniformBlueNoiseSeeds(float maxDistFromSurface); 

    // SDF grid computation and sampling
    void buildSDFGrid(const Model& volumeMesh);
    float sampleSDFGrid(const glm::vec3& pos) const;
    float computePointToMeshDistance(const glm::vec3& point, const Model& model) const;
    glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, 
                                     const glm::vec3& b, const glm::vec3& c) const;
};
