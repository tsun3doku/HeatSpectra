#pragma once
#include <glm/glm.hpp>
#include <set>
#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"
#include "Model.hpp"
#include "Structs.hpp"
#include "CommonSubdivision.hpp"
#include "SupportingHalfedge.hpp"

// Forward declarations
class VulkanDevice;
class MemoryAllocator;

class iODT {
public:
    iODT(Model& model, VulkanDevice& vulkanDevice, MemoryAllocator& allocator);
    ~iODT();
    
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
    bool optimalDelaunayTriangulation(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize);
    void repositionInsertedVertices(int iterations, double tol, double maxEdgeLength, double stepSize);

    // Refinement operations
    bool delaunayRefinement(int maxIters, float minAngleDegrees);
    std::vector<RefinementCandidate> findRefinementCandidates(float minAngleThreshold, float maxAreaThreshold, float minAreaThreshold);
    bool insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex);
    bool insertPoint(uint32_t faceIdx, const glm::dvec3& baryCoords, uint32_t& outVertex, bool* outWasInserted = nullptr);
    bool splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t = 0.5);
    bool splitEdge(uint32_t heEdge, double tParam, uint32_t& outNewV, bool* outWasInserted = nullptr);

    bool computeWeightedCircumcenter(uint32_t vertIdx, uint32_t& outRefFace, int& outLocalRefIdx, glm::dvec2& outAvgVec, double& outAvgLen);
    bool resolveVertex(uint32_t newVertexIdx, const GeodesicTracer::SurfacePoint& intrinsicPoint);

    double computeMinAngle( uint32_t faceIdx);

    void initializeVertexLocations();
    GeodesicTracer::GeodesicTraceResult traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx);
    
    void updateVertexLocation(uint32_t intrinsicVertexId, const GeodesicTracer::SurfacePoint& locationOnInput);
    
    void createCommonSubdivision(Model& overlayModel, std::vector<CommonSubdivision::IntrinsicTriangle>& outIntrinsicTriangles);
    void saveCommonSubdivisionOBJ(const std::string& filename, const Model& overlayModel) const;

    std::vector<glm::vec3> getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const;
    const CommonSubdivision* getCommonSubdivisionObject() const { return commonSubdivision.get(); }
    
    void cleanup();

    // Supporting halfedge is always available after construction
    SupportingHalfedge* getSupportingHalfedge() { return supportingHalfedge.get(); }
    const SupportingHalfedge* getSupportingHalfedge() const { return supportingHalfedge.get(); }

private:
    Model& model;                   // Base input model
    SignpostMesh intrinsicMesh;     // Intrinsic mesh 
    SignpostMesh inputMesh;         // Input mesh (immutable)
    VulkanDevice& vulkanDevice;     
    MemoryAllocator& allocator;    
    GeodesicTracer tracer;          // Tracer for the intrinsic mesh
    GeodesicTracer tracerInput;     // Tracer for the input mesh

    std::set<std::pair<uint32_t, uint32_t>> recentlySplit;
    std::unordered_set<uint32_t> insertedVertices;
    
    // Maps every intrinsic vertex to a location on the original mesh surface
    std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint> intrinsicVertexLocations;
    
    // Tracks which input face was used during vertex resolution
    std::unordered_map<uint32_t, uint32_t> vertexResolutionFaces;
    
    std::unique_ptr<CommonSubdivision> commonSubdivision;   
    std::unique_ptr<SupportingHalfedge> supportingHalfedge;
    
    bool isEdgeOriginal(uint32_t edgeIdx) const;
};
