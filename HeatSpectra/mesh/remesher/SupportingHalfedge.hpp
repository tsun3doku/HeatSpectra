#pragma once

#include "HalfEdgeMesh.hpp"
#include "SignPostMesh.hpp"
#include "GeodesicTracer.hpp"
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// 
//                          [ This DS enables GPU based rendering of intrinsic triangulations
//                            Tracks for each input halfedge:
//                            - Which intrinsic HE currently supports it
//                            - The angular offset between the input halfedge and support HE ]

class SupportingHalfedge {
public:
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    struct SupportingInfo {
        uint32_t supportingHE = INVALID_INDEX;  // Which intrinsic halfedge supports this input halfedge
        double supportingAngle = 0.0;           // Angular offset 
    };

    struct IntrinsicVertex {
        uint32_t intrinsicVertexId;         // ID in intrinsic mesh
        glm::vec3 position;                 // 3D world position
        glm::vec3 normal;                   // Area-weighted vertex normal
        uint32_t inputLocationType;         // 0=VERTEX, 1=EDGE, 2=FACE
        uint32_t inputElementId;            // Which input vertex/edge/face
        glm::vec3 inputBaryCoords;          // Position on that element
    };

    struct IntrinsicTriangle {
        glm::vec3 center;           // Triangle centroid
        glm::vec3 normal;           // Face normal (normalized)
        float area;                 // Triangle area
        uint32_t vertexIndices[3];  // Indices into vertex array
        uint32_t faceId;            // Intrinsic face ID
    };

    struct IntrinsicMesh {
        std::vector<IntrinsicVertex> vertices;      // Deduplicated vertices (one per unique vertex)
        std::vector<uint32_t> indices;              // Triangle indices (3 per triangle)
        std::vector<uint32_t> faceIds;              // Intrinsic face ID per triangle
        std::vector<IntrinsicTriangle> triangles;   // Triangle geometric data
    };

    struct GPUResources {
        size_t triangleCount = 0;
        size_t vertexCount = 0;
        float averageTriangleArea = 0.0f;

        VkBuffer bufferS = VK_NULL_HANDLE;
        VkBuffer bufferA = VK_NULL_HANDLE;
        VkBuffer bufferH = VK_NULL_HANDLE;
        VkBuffer bufferE = VK_NULL_HANDLE;
        VkBuffer bufferT = VK_NULL_HANDLE;
        VkBuffer bufferL = VK_NULL_HANDLE;
        VkBuffer bufferHInput = VK_NULL_HANDLE;
        VkBuffer bufferEInput = VK_NULL_HANDLE;
        VkBuffer bufferTInput = VK_NULL_HANDLE;
        VkBuffer bufferLInput = VK_NULL_HANDLE;
        VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
        VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;

        VkDeviceSize offsetS = 0;
        VkDeviceSize offsetA = 0;
        VkDeviceSize offsetH = 0;
        VkDeviceSize offsetE = 0;
        VkDeviceSize offsetT = 0;
        VkDeviceSize offsetL = 0;
        VkDeviceSize offsetHInput = 0;
        VkDeviceSize offsetEInput = 0;
        VkDeviceSize offsetTInput = 0;
        VkDeviceSize offsetLInput = 0;
        VkDeviceSize triangleGeometryOffset = 0;
        VkDeviceSize vertexGeometryOffset = 0;

        VkBufferView viewS = VK_NULL_HANDLE;
        VkBufferView viewA = VK_NULL_HANDLE;
        VkBufferView viewH = VK_NULL_HANDLE;
        VkBufferView viewE = VK_NULL_HANDLE;
        VkBufferView viewT = VK_NULL_HANDLE;
        VkBufferView viewL = VK_NULL_HANDLE;
        VkBufferView viewHInput = VK_NULL_HANDLE;
        VkBufferView viewEInput = VK_NULL_HANDLE;
        VkBufferView viewTInput = VK_NULL_HANDLE;
        VkBufferView viewLInput = VK_NULL_HANDLE;
    };

    struct GPUBuffers {
        std::vector<int32_t> supportingHalfedges;
        std::vector<float> supportingAngles;
        std::vector<int32_t> intrinsicHalfedgeData;
        std::vector<int32_t> intrinsicEdgeData;
        std::vector<int32_t> intrinsicTriangleData;
        std::vector<float> intrinsicLengths;
        std::vector<int32_t> inputHalfedgeData;
        std::vector<int32_t> inputEdgeData;
        std::vector<int32_t> inputTriangleData;
        std::vector<float> inputLengths;
    };

    struct InsertedVertexLocation {
        uint32_t locationType;      // 0=VERTEX, 1=EDGE, 2=FACE
        uint32_t elementId;         // Input vertex/edge/face index
        glm::vec3 baryCoords;       // Barycentric coordinates
    };

    SupportingHalfedge(const SignpostMesh& inputMesh, SignpostMesh& intrinsicMesh, const GeodesicTracer& tracer);
    ~SupportingHalfedge();

    void initialize();
    void updateRemoval(uint32_t intrinsicHE);
    void updateInsertion(uint32_t intrinsicHE);
    bool flipEdge(uint32_t edgeIdx);
    int makeDelaunay(int maxIterations, std::vector<uint32_t>* flippedEdges = nullptr);
    int makeDelaunayLocal(int maxIterations, const std::vector<uint32_t>& seedEdges, std::vector<uint32_t>* flippedEdges = nullptr);

    // Track where inserted vertices are located on the input mesh
    void trackInsertedVertex(uint32_t vertexIdx, const struct GeodesicTracer::SurfacePoint& surfacePoint);

    const SupportingInfo& getSupportingInfo(uint32_t inputHalfedgeIdx) const;

    GPUBuffers buildGPUBuffers() const;
    IntrinsicMesh buildIntrinsicMesh() const;
    void calculateVertexNormals(IntrinsicMesh& meshData) const;

    const std::vector<SupportingInfo>& getAllSupportingInfo() const { return supportingInfoPerHalfedge; }

private:
    void clampSupportingAngle(SupportingInfo& info) const;

    const SignpostMesh& inputMesh;
    SignpostMesh& intrinsicMesh;
    const GeodesicTracer& tracer;

    std::vector<SupportingInfo> supportingInfoPerHalfedge;     // One entry per input halfedge
    std::unordered_map<uint32_t, InsertedVertexLocation> insertedVertexLocations;
};
