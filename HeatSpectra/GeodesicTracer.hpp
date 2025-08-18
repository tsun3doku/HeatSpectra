#pragma once
#include <glm/glm.hpp>
#include <set>

#include "SignPostMesh.hpp"

class SignpostMesh;

class GeodesicTracer {
public:
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);
    static constexpr double EDGE_SNAP_MIN = 1e-3;
    GeodesicTracer(SignpostMesh& mesh);

    struct SurfacePoint {
        enum class Type { VERTEX, EDGE, FACE };
        Type type;                              // Element type
        uint32_t elementId;                     // Index of original vertex, edge, or face
        glm::dvec3 baryCoords;                  // Barycentric coordinates
        double split;                           // Split fraction along edge

        // Face constructor
        SurfacePoint(uint32_t faceId, glm::dvec3 const& bary)
            : type(Type::FACE), elementId(faceId), baryCoords(bary), split(0.0) {}

        // Edge constructor
        SurfacePoint(uint32_t edgeId, double t)
            : type(Type::EDGE), elementId(edgeId), baryCoords(0), split(t) {}

        // Vertex constructor
        SurfacePoint(uint32_t vertId)
            : type(Type::VERTEX), elementId(vertId), baryCoords(1, 0, 0), split(0.0) {}

        SurfacePoint() 
            : type(Type::VERTEX), elementId(0), baryCoords(1, 0, 0), split(0.0) {}
    };

    struct FaceStepResult {
        bool success;                           // Ray step complete success/error
        bool hitEdge;                           // True if edge hit, false if remains in face
        bool hitVertex;                         // True if vertex hit
        uint32_t halfEdgeIdx;                   // halfEdge index crossed/hit
        uint32_t vertexIdx;                     // Vertex index crossed/hit
        int localEdgeIndex;                     // Local face edge index (0,1,2)
        glm::dvec3 finalBary;                   // Final barycentric coordinates after step
        glm::dvec2 dir2D;                       // Outgoing 2D direction
        double distanceTraveled;                // Distance traveled after step
        double edgeParam;                       // Trace hit location along edge (0-1)

        FaceStepResult() 
            : success(false), hitEdge(false), hitVertex(false), halfEdgeIdx(INVALID_INDEX), vertexIdx(INVALID_INDEX), 
            localEdgeIndex(-1), finalBary(0.0), dir2D(0.0), distanceTraveled(0.0), edgeParam(0.0) {}
    };

    struct GeodesicTraceResult {
        bool success;                           // Trace complete success/error
        glm::vec3 position3D;                   // 3D point on the input mesh
        glm::dvec3 baryCoords;                  // Final barycentric coordinates
        double distance;                        // Geodesic distance traveled
        uint32_t finalFaceIdx;                  // Face index the trace lands on
        std::vector<FaceStepResult> steps;
        SurfacePoint exitPoint;                 // Surface type of trace target point

        GeodesicTraceResult() 
            : success(false), position3D(0.0,0.0,0.0), baryCoords(0.0,0.0,0.0), distance(0.0), finalFaceIdx(INVALID_INDEX), exitPoint() {}
    };

    struct GeodesicState {
        uint32_t currentFace;                   // Current face trace is on
        glm::dvec2 currentPoint2D;              // Position in current face's 2D layout
        glm::dvec2 direction2D;                 // Direction in current face's 2D layout
        double remainingDistance;                   
        bool isValid;                               

        GeodesicState() 
            : currentFace(INVALID_INDEX), currentPoint2D(0.0), direction2D(0.0), remainingDistance(0.0), isValid(true) {}
    };

    struct GeodesicTraceOptions {
        int maxIters                            = 100;
        bool includePath                        = false;
    };

    GeodesicTraceResult traceFromVertex(uint32_t vertexIdx, uint32_t refFace, const glm::dvec2& dirInRefFace, double remaining, GeodesicTraceResult& baseResult, double totalLength) const;
    GeodesicTraceResult traceFromFace(uint32_t startFaceIdx, const glm::dvec3& startBary, const glm::dvec2& cartesianDir, double length) const;
    FaceStepResult traceInFace(const SurfacePoint& startP, const glm::dvec2& dir2D, double length) const;

    glm::dvec3 evaluateSurfacePoint(const SurfacePoint& point) const;
    glm::dvec2 chartLocal2D(const SignpostMesh& mesh, uint32_t oldFaceIdx, uint32_t newFaceIdx, const glm::dvec2& oldPoint2D) const;
    glm::dvec2 rotateVectorAcrossEdge(const SignpostMesh& mesh, uint32_t oldFace, uint32_t oldHe, uint32_t newFace, uint32_t newHe, const glm::dvec2& vecInOld) const;
    bool solveRayEdge(const glm::dvec2& rayDir, const glm::dvec2& edgeVec, const glm::dvec2& b, double& out_t, double& out_u) const;
    bool resolveVertexLinear(uint32_t newV, const SurfacePoint& location, uint32_t heA = INVALID_INDEX, uint32_t heB = INVALID_INDEX);
    bool resolveVertex(uint32_t startFace, uint32_t startHe, const GeodesicTraceResult& result, glm::dvec3& outPos3D);

    glm::dvec2 computeTangentVector(uint32_t startVertex, const SurfacePoint& target);
    uint32_t findStartingHalfEdge(uint32_t startVertex, uint32_t faceIdx, const glm::dvec2& tangentVector) const;

private:
    SignpostMesh& mesh;
    GeodesicTraceOptions options;
};