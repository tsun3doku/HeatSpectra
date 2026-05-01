#pragma once
#include <glm/glm.hpp>
#include <queue>
#include <deque>
#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"
#include "util/Structs.hpp"
#include "CommonSubdivision.hpp"
#include "SupportingHalfedge.hpp"

class iODT {
public:
    iODT(const std::vector<float>& pointPositions, const std::vector<uint32_t>& triangleIndices);
    ~iODT();
    
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    bool optimalDelaunayTriangulation(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize);
    bool insertPoint(uint32_t faceIdx, const glm::dvec3& baryCoords, uint32_t& outVertex, bool* outWasInserted = nullptr);

    GeodesicTracer::GeodesicTraceResult traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx);

    void createCommonSubdivision(Model& overlayModel, std::vector<CommonSubdivision::IntrinsicTriangle>& outIntrinsicTriangles);
    void saveCommonSubdivisionOBJ(const std::string& filename, const Model& overlayModel) const;

    std::vector<glm::vec3> getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const;
    const CommonSubdivision* getCommonSubdivisionObject() const { return commonSubdivision.get(); }
    
    SupportingHalfedge* getSupportingHalfedge() { return supportingHalfedge.get(); }
    const SupportingHalfedge* getSupportingHalfedge() const { return supportingHalfedge.get(); }

    void cleanup();

private:
    void refreshIntrinsicDirectionalData();
    void optimalReposition(int iterations, double tol, double maxEdgeLength, double stepSize);
    int splitLongEdges(double maxEdgeLength, int maxSplits);
    double repositionInsertedVertices(double stepSize);
    bool delaunayRefinement(int maxIters, float minAngleDegrees);

    struct FaceCandidate {
        uint32_t faceIdx = INVALID_INDEX;
        float area = 0.0f;
        float priority = 0.0f;

        bool operator<(const FaceCandidate& other) const {
            return priority < other.priority;
        }
    };

    std::vector<uint32_t> collectLocalDelaunayEdges(HalfEdgeMesh& conn, uint32_t vertexIdx);
    void updateLocalFaces(HalfEdgeMesh& conn, const std::vector<uint32_t>& facePatch);
    void updateLocalEdges(HalfEdgeMesh& conn, const std::vector<uint32_t>& edgePatch);
    bool isValidFace(uint32_t faceIdx) const;
    bool isValidEdge(uint32_t edgeIdx) const;
    bool needsRefinement(uint32_t faceIdx, float minAngleThreshold, float minAreaThreshold);
    float refinementPriority(uint32_t faceIdx);
    void queueRefineFace(uint32_t faceIdx, float minAngleThreshold, float minAreaThreshold, std::priority_queue<FaceCandidate>& faceQueue);
    void queueDelaunayEdge(uint32_t edgeIdx, std::deque<uint32_t>& edgeQueue, std::vector<uint8_t>& inQueue);

    bool insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex);
    bool splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t = 0.5);
    bool splitEdge(uint32_t heEdge, double tParam, uint32_t& outNewV, bool* outWasInserted = nullptr);

    bool computeWeightedCircumcenter(uint32_t vertIdx, glm::dvec2& outAvgVec, double& outAvgLen);
    bool resolveVertex(uint32_t newVertexIdx);

    double computeMinAngle(uint32_t faceIdx);
    bool isEdgeOriginal(uint32_t edgeIdx) const;

    void initializeVertexLocations();
    SignpostMesh intrinsicMesh;     // Intrinsic mesh 
    SignpostMesh inputMesh;         // Input mesh 
    GeodesicTracer tracer;          // Tracer for the intrinsic mesh
    GeodesicTracer tracerInput;     // Tracer for the input mesh

    std::unordered_set<uint32_t> insertedVertices;
    
    // Maps every intrinsic vertex to a location on the original mesh surface
    std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint> intrinsicVertexLocations;

    // Tracks which input face was used during vertex resolution
    std::unordered_map<uint32_t, uint32_t> vertexResolutionFaces;

    std::unique_ptr<CommonSubdivision> commonSubdivision;   
    std::unique_ptr<SupportingHalfedge> supportingHalfedge; 
};


