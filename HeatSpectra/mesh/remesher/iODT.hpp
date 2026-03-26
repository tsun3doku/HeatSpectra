#pragma once
#include <glm/glm.hpp>
#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"
#include "scene/Model.hpp"
#include "util/Structs.hpp"
#include "CommonSubdivision.hpp"
#include "SupportingHalfedge.hpp"

class VulkanDevice;
class MemoryAllocator;
struct GeometryData;

class iODT {
public:
    iODT(Model& model, VulkanDevice& vulkanDevice, MemoryAllocator& allocator);
    iODT(const GeometryData& geometry, VulkanDevice& vulkanDevice, MemoryAllocator& allocator);
    ~iODT();
    
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    enum class RefinementType {
        CIRCUMCENTER_INSERTION,
        EDGE_SPLIT
    };

    struct RefinementCandidate {
        RefinementType type;
        uint32_t faceIdx        = 0;
        uint32_t edgeIdx        = 0;
        float priority          = 0.0f;
        float minAngle          = 0.0f;
        float area              = 0.0f;
    };

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

    std::vector<RefinementCandidate> findRefinementCandidates(float minAngleThreshold, float minAreaThreshold);
    std::vector<RefinementCandidate> scanCandidateRange(size_t start, size_t end, float minAngleThreshold, float minAreaThreshold);

    static bool byPriorityDescending(const RefinementCandidate& a, const RefinementCandidate& b);
    void markSkipFaces(HalfEdgeMesh& conn, uint32_t vertexIdx, std::unordered_set<uint32_t>& skipFaces);
    std::vector<uint32_t> collectLocalDelaunayEdges(HalfEdgeMesh& conn, uint32_t vertexIdx);
    void refreshIntrinsicDirectionalFaces(HalfEdgeMesh& conn, const std::vector<uint32_t>& facePatch);
    void refreshIntrinsicDirectionalPatch(HalfEdgeMesh& conn, const std::vector<uint32_t>& edgePatch);

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
    VulkanDevice& vulkanDevice;     
    MemoryAllocator& allocator;    
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
