#pragma once

#include "HalfEdgeMesh.hpp"
#include "SignPostMesh.hpp"
#include "GeodesicTracer.hpp"
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

class VulkanDevice;
class MemoryAllocator;

// 
//                          [ This DS enables GPU based rendering of intrinsic triangulations
//                            Tracks for each input triangle:
//                            - Which intrinsic HE currently supports it
//                            - The angular offset between first input edge and support HE ]

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

    struct InsertedVertexLocation {
        uint32_t locationType;      // 0=VERTEX, 1=EDGE, 2=FACE
        uint32_t elementId;         // Input vertex/edge/face index
        glm::vec3 baryCoords;       // Barycentric coordinates
    };

    struct GPUBuffers {
        std::vector<int32_t> S;      // Supporting halfedge: S[inputTriIdx] = intrinsicHalfedgeIdx
        std::vector<float> A;        // Supporting angle: A[inputTriIdx] = angle
        std::vector<int32_t> H;      // Intrinsic halfedge data [origin, edge, face, next]
        std::vector<int32_t> E;      // Intrinsic edge data [he0, he1]
        std::vector<int32_t> T;      // Intrinsic triangle data [halfedge]
        std::vector<float> L;        // Intrinsic edge lengths

        // Input mesh data 
        std::vector<int32_t> H_input;               // Input halfedge data [origin, edge, face, next]
        std::vector<int32_t> E_input;               // Input edge data [he0, he1]
        std::vector<int32_t> T_input;               // Input triangle data [halfedge]
        std::vector<float> L_input;                 // Input edge lengths
    };

    SupportingHalfedge(const SignpostMesh& inputMesh, SignpostMesh& intrinsicMesh, const GeodesicTracer& tracer,
        class VulkanDevice& vulkanDevice, class MemoryAllocator& allocator);
    ~SupportingHalfedge();

    void initialize();
    void updateRemoval(uint32_t intrinsicHE);
    void updateInsertion(uint32_t intrinsicHE);
    bool flipEdge(uint32_t edgeIdx);
    int makeDelaunay(int maxIterations, std::vector<uint32_t>* flippedEdges = nullptr);

    // Track where inserted vertices are located on the input mesh
    void trackInsertedVertex(uint32_t vertexIdx, const class GeodesicTracer::SurfacePoint& surfacePoint);

    const SupportingInfo& getSupportingInfo(uint32_t inputTriangleIdx) const;

    IntrinsicMesh buildIntrinsicMesh() const;
    void calculateVertexNormals(IntrinsicMesh& meshData) const;
    GPUBuffers buildGPUBuffers() const;
    void uploadToGPU();
    void uploadIntrinsicTriangleData();
    void uploadIntrinsicVertexData();
    void cleanup();
    void invalidateIntrinsicMeshCache() { intrinsicMeshCacheValid = false; }

    const std::vector<SupportingInfo>& getAllSupportingInfo() const { return supportingInfo; }
    double getIntrinsicCornerAngle(uint32_t intrinsicHE) const;
    bool isUploadedToGPU() const { return gpuDataUploaded; }

    VkBufferView getSupportingHalfedgeView() const { return bufferViewS; }
    VkBufferView getSupportingAngleView() const { return bufferViewA; }
    VkBufferView getHalfedgeView() const { return bufferViewH; }
    VkBufferView getEdgeView() const { return bufferViewE; }
    VkBufferView getTriangleView() const { return bufferViewT; }
    VkBufferView getLengthView() const { return bufferViewL; }
    VkBufferView getInputHalfedgeView() const { return bufferViewH_input; }
    VkBufferView getInputEdgeView() const { return bufferViewE_input; }
    VkBufferView getInputTriangleView() const { return bufferViewT_input; }
    VkBufferView getInputLengthView() const { return bufferViewL_input; }

    VkBuffer getIntrinsicTriangleBuffer() const { return intrinsicTriangleBuffer; }
    VkDeviceSize getTriangleGeometryOffset() const { return triangleGeometryOffset; }
    size_t getTriangleCount() const { return cachedIntrinsicMesh.triangles.size(); }
    float getAverageTriangleArea() const;

    VkBuffer getIntrinsicVertexBuffer() const { return intrinsicVertexBuffer; }
    VkDeviceSize getVertexGeometryOffset() const { return vertexGeometryOffset; }
    size_t getVertexCount() const { return cachedIntrinsicMesh.vertices.size(); }

private:
    const SignpostMesh& inputMesh;
    SignpostMesh& intrinsicMesh;
    const GeodesicTracer& tracer;
    VulkanDevice& vulkanDevice;
    MemoryAllocator& allocator;

    std::vector<SupportingInfo> supportingInfo;     // One entry per input triangle
    std::unordered_map<uint32_t, InsertedVertexLocation> insertedVertexLocations;

    bool gpuDataUploaded = false;
    mutable IntrinsicMesh cachedIntrinsicMesh;
    mutable bool intrinsicMeshCacheValid = false;

    VkBuffer bufferS = VK_NULL_HANDLE;                  // Supporting halfedge buffer
    VkBuffer bufferA = VK_NULL_HANDLE;                  // Supporting angle buffer
    VkBuffer bufferH = VK_NULL_HANDLE;                  // Intrinsic halfedge data buffer
    VkBuffer bufferE = VK_NULL_HANDLE;                  // Intrinsic edge data buffer
    VkBuffer bufferT = VK_NULL_HANDLE;                  // Intrinsic triangle data buffer
    VkBuffer bufferL = VK_NULL_HANDLE;                  // Intrinsic edge length buffer
    VkBuffer bufferH_input = VK_NULL_HANDLE;            // Input halfedge data buffer
    VkBuffer bufferE_input = VK_NULL_HANDLE;            // Input edge data buffer
    VkBuffer bufferT_input = VK_NULL_HANDLE;            // Input triangle data buffer
    VkBuffer bufferL_input = VK_NULL_HANDLE;            // Input edge length buffer
    VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
    VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;  

    VkDeviceSize offsetS = 0;
    VkDeviceSize offsetA = 0;
    VkDeviceSize offsetH = 0;
    VkDeviceSize offsetE = 0;
    VkDeviceSize offsetT = 0;
    VkDeviceSize offsetL = 0;
    VkDeviceSize offsetH_input = 0;
    VkDeviceSize offsetE_input = 0;
    VkDeviceSize offsetT_input = 0;
    VkDeviceSize offsetL_input = 0;
    VkDeviceSize triangleGeometryOffset = 0;
    VkDeviceSize vertexGeometryOffset = 0;

    VkBufferView bufferViewS = VK_NULL_HANDLE;
    VkBufferView bufferViewA = VK_NULL_HANDLE;
    VkBufferView bufferViewH = VK_NULL_HANDLE;
    VkBufferView bufferViewE = VK_NULL_HANDLE;
    VkBufferView bufferViewT = VK_NULL_HANDLE;
    VkBufferView bufferViewL = VK_NULL_HANDLE;
    VkBufferView bufferViewH_input = VK_NULL_HANDLE;
    VkBufferView bufferViewE_input = VK_NULL_HANDLE;
    VkBufferView bufferViewT_input = VK_NULL_HANDLE;
    VkBufferView bufferViewL_input = VK_NULL_HANDLE;
};
