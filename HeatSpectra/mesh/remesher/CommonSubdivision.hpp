#pragma once

#include "SignPostMesh.hpp"
#include "GeodesicTracer.hpp"
#include "scene/Model.hpp"
#include "util/Structs.hpp"
#include <vector>
#include <array>
#include <unordered_map>
#include <glm/glm.hpp>

class iODT;

class CommonSubdivision {
public:
    struct IntrinsicTriangle {
        uint32_t faceIdx;                                           // Index of intrinsic face
        std::array<uint32_t, 3> intrinsicVertices;                  // Intrinsic vertex IDs at each corner before merging
        std::vector<GeodesicTracer::SurfacePoint> vertices;         // Intrinsic and subdivided vertices 
        std::vector<glm::vec3> positions;                           // 3D positions of vertices
        std::vector<uint32_t> indices;                              // Indices of 'vertices' after merging
        std::vector<uint32_t> triangulationIndices;                 // Local triangulation indices (into 'positions')
        std::array<size_t, 3> cornerIndices;                        // Corner 'indices' of intrinsic face
        std::vector<glm::dvec3> baryCoords;                         // Barycentric coords relative to intrinsic face
        
        IntrinsicTriangle() : faceIdx(static_cast<uint32_t>(-1)) {
            intrinsicVertices.fill(static_cast<uint32_t>(-1));
            cornerIndices.fill(0);
        }
    };

    struct CommonVertex {
        uint32_t isCorner;              // 1 if corner vertex, 0 if subdivision vertex
        uint32_t corners[3];            // Corner vertices to interpolate from 
        glm::vec4 baryCoords;           // Barycentric coords 
        
        CommonVertex() : isCorner(0), corners{0, 0, 0}, baryCoords(0, 0, 0, 0) {}
    };

    CommonSubdivision(
        const SignpostMesh& intrinsicMesh,
        const SignpostMesh& inputMesh,
        const GeodesicTracer& tracer,
        const std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint>& vertexLocations,
        iODT& remesher
    );

    void build();
    IntrinsicTriangle buildIntrinsicTriangle(uint32_t intrinsicFaceIdx);
    std::vector<glm::vec3> mergeNearbyPoints(const std::vector<glm::vec3>& points, double tolerance, std::vector<uint32_t>& outMapping) const;
    std::vector<glm::vec3> generateFaceColors(size_t count) const;
    
    std::vector<CommonVertex> buildVertexData() const;
  
    void exportToModel(Model& overlayModel) const;

    const std::vector<IntrinsicTriangle>& getIntrinsicTriangles() const { return intrinsicTriangles; }
    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }

private:
    const SignpostMesh& intrinsicMesh;
    const SignpostMesh& inputMesh;
    const GeodesicTracer& tracer;
    const std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint>& vertexLocations;
    iODT* remesher;
    
    std::vector<IntrinsicTriangle> intrinsicTriangles;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
