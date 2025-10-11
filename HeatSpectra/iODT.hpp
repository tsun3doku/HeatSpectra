#pragma once
#include <glm/glm.hpp>
#include <set>

#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"
#include "Model.hpp"
#include "Structs.hpp"


class iODT {
public:
    iODT(Model& model, SignpostMesh& mesh);

    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    enum class RefinementType {
        CIRCUMCENTER_INSERTION,
        EDGE_SPLIT
    };
    struct pair_hash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };
    struct RefinementCandidate {
        RefinementType type;
        uint32_t faceIdx        = 0;
        uint32_t edgeIdx        = 0;
        float priority          = 0.0f;
        float minAngle          = 0.0f;
        float area              = 0.0f;
        float quality           = 0.0f;
    };

    // High level ODT functions
    bool optimalDelaunayTriangulation(int iterations);
    void repositionInsertedVertices(int iterations, double tol, double maxEdgeLength);

    // Refinement operations
    bool delaunayRefinement();
    std::vector<RefinementCandidate> findRefinementCandidates( float minAngleThreshold, float maxAreaThreshold);
    bool insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex);
    bool splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t = 0.5);

    // Helpers
    bool computeWeightedCircumcenter(uint32_t vertIdx, uint32_t& outRefFace, int& outLocalRefIdx, glm::dvec2& outAvgVec, double& outAvgLen);
    bool resolveVertex(uint32_t newVertexIdx, const GeodesicTracer::SurfacePoint& intrinsicPoint);

    // Quality metrics
    double computeMinAngle( uint32_t faceIdx);

    // Intrinsic edge tracing 
    void initializeVertexLocations();
    std::vector<GeodesicTracer::SurfacePoint> traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx);
    
    // Correspondence mapping functions
    void updateVertexLocation(uint32_t intrinsicVertexId, const GeodesicTracer::SurfacePoint& locationOnInput);

    // Visualization helpers
    void createCommonSubdivision(Model& overlayModel) const;
    std::vector<glm::vec3> getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const;
    void saveCommonSubdivisionOBJ(const std::string& filename, const Model& overlayModel) const;
    std::vector<glm::vec3> mergeNearbyPoints(const std::vector<glm::vec3>& points, double tolerance) const;

private:
    Model& model;                   // Base input model
    SignpostMesh& mesh;             // Intrinsic mesh
    SignpostMesh inputMesh;         // Input mesh (unchanged connectivity)
    GeodesicTracer tracer;          // Tracer for the intrinsic mesh
    GeodesicTracer tracerInput;     // Tracer for the input mesh

    std::set<std::pair<uint32_t, uint32_t>> recentlySplit;
    std::unordered_set<uint32_t> insertedVertices;
    
    // Maps every intrinsic vertex to a location on the original mesh surface
    std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint> intrinsicVertexLocations;
    
    // Tracks which input face was used during vertex resolution
    std::unordered_map<uint32_t, uint32_t> vertexResolutionFaces;
    
    bool isEdgeOriginal(uint32_t edgeIdx) const;
};
