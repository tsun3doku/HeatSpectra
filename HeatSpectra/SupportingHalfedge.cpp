#include "SupportingHalfedge.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanBuffer.hpp"
#include "Structs.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <map>
#include <queue>
#include <set>
#include <unordered_set>

SupportingHalfedge::SupportingHalfedge(const SignpostMesh& inputMesh, SignpostMesh& intrinsicMesh, const GeodesicTracer& tracer,
    VulkanDevice& vulkanDevice, MemoryAllocator& allocator)
    : inputMesh(inputMesh)
    , intrinsicMesh(intrinsicMesh)
    , tracer(tracer)
    , vulkanDevice(vulkanDevice)
    , allocator(allocator)
{
}

SupportingHalfedge::~SupportingHalfedge() {
    cleanup();
}

void SupportingHalfedge::initialize() {
    const auto& inputConn = inputMesh.getConnectivity();
    const auto& inputFaces = inputConn.getFaces();

    const auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicFaces = intrinsicConn.getFaces();

    supportingInfo.resize(inputFaces.size());

    // For each input triangle, calculate the angle from its reference edge to the supporting halfedge
    int invalidCount = 0;
    const auto& inputHalfedges = inputConn.getHalfEdges();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

    for (size_t faceIdx = 0; faceIdx < inputFaces.size(); ++faceIdx) {
        // Get the reference halfedge of the input triangle
        uint32_t inputRefHE = inputFaces[faceIdx].halfEdgeIdx;

        // Get the halfedge from the intrinsic mesh
        uint32_t he_intrinsic = intrinsicFaces[faceIdx].halfEdgeIdx;

        if (he_intrinsic == INVALID_INDEX || inputRefHE == INVALID_INDEX) {
            supportingInfo[faceIdx].supportingHE = INVALID_INDEX;
            supportingInfo[faceIdx].supportingAngle = 0.0;
            invalidCount++;
        }
        else {
            supportingInfo[faceIdx].supportingHE = he_intrinsic;

            // Get 2D vectors in the triangle's local frame
            const auto& inputHVF = inputMesh.getHalfedgeVectorsInFace();
            const auto& intrinsicHVF = intrinsicMesh.getHalfedgeVectorsInFace();

            if (inputRefHE < inputHVF.size() && he_intrinsic < intrinsicHVF.size()) {
                glm::dvec2 inputDir = inputHVF[inputRefHE];
                glm::dvec2 intrinsicDir = intrinsicHVF[he_intrinsic];

                // Compute angles in the 2D space 
                double inputAngle = std::atan2(inputDir.y, inputDir.x);
                double intrinsicAngle = std::atan2(intrinsicDir.y, intrinsicDir.x);

                // phi0 is the angle from the first input edge to intrinsic supporting edge
                double phi0 = intrinsicAngle - inputAngle;

                // Normalize to (-pi, pi]
                while (phi0 > glm::pi<double>()) phi0 -= 2.0 * glm::pi<double>();
                while (phi0 <= -glm::pi<double>()) phi0 += 2.0 * glm::pi<double>();

                supportingInfo[faceIdx].supportingAngle = phi0;
            }
            else {
                supportingInfo[faceIdx].supportingAngle = 0.0;
            }
        }
    }

    // Invalidate cached intrinsic mesh 
    intrinsicMeshCacheValid = false;
}

void SupportingHalfedge::updateRemoval(uint32_t intrinsicHE) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

    if (intrinsicHE >= intrinsicHalfedges.size()) {
        return;
    }

    uint32_t v = intrinsicHalfedges[intrinsicHE].origin;

    const auto& inputConn = inputMesh.getConnectivity();
    const auto& inputVertices = inputConn.getVertices();

    if (v >= inputVertices.size()) {
        return;
    }

    std::vector<uint32_t> helist = inputConn.getVertexHalfEdges(v);
    const auto& inputHalfedges = inputConn.getHalfEdges();

    for (uint32_t he : helist) {
        if (he >= inputHalfedges.size()) continue;

        uint32_t te = inputHalfedges[he].face;
        if (te >= supportingInfo.size()) continue;

        if (supportingInfo[te].supportingHE == intrinsicHE) {

            uint32_t mate_h = intrinsicHalfedges[intrinsicHE].opposite;
            if (mate_h == INVALID_INDEX) continue;
            uint32_t new_ref = intrinsicHalfedges[mate_h].next;
            if (new_ref == INVALID_INDEX) continue;

            supportingInfo[te].supportingHE = new_ref;

            double theta = intrinsicHalfedges[new_ref].cornerAngle;
            supportingInfo[te].supportingAngle -= theta;
        }
    }

    // Invalidate cached intrinsic mesh
    intrinsicMeshCacheValid = false;
}

void SupportingHalfedge::updateInsertion(uint32_t h) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

    if (h >= intrinsicHalfedges.size()) {
        return;
    }

    uint32_t v = intrinsicHalfedges[h].origin;

    const auto& inputConn = inputMesh.getConnectivity();
    const auto& inputHalfedges = inputConn.getHalfEdges();
    const auto& inputVertices = inputConn.getVertices();

    if (v >= inputVertices.size()) {
        return;
    }

    std::vector<uint32_t> helist = inputConn.getVertexHalfEdges(v);

    for (uint32_t he : helist) {
        if (he >= inputHalfedges.size()) {
            continue;
        }

        uint32_t te = inputHalfedges[he].face;
        if (te >= supportingInfo.size()) {
            continue;
        }

        uint32_t ref = supportingInfo[te].supportingHE;
        if (ref == INVALID_INDEX || ref >= intrinsicHalfedges.size()) {
            continue;
        }

        uint32_t prevRef = intrinsicHalfedges[ref].prev;
        if (prevRef == INVALID_INDEX || prevRef >= intrinsicHalfedges.size()) {
            continue;
        }

        uint32_t matePrev = intrinsicHalfedges[prevRef].opposite;

        if (matePrev == h) {
            double theta = intrinsicHalfedges[ref].cornerAngle;

            double newAngle = supportingInfo[te].supportingAngle + theta;

            if (newAngle <= 0.0) {
                supportingInfo[te].supportingHE = h;

                supportingInfo[te].supportingAngle = newAngle;
            }
        }
    }

    // Invalidate cached intrinsic mesh
    intrinsicMeshCacheValid = false;
}

bool SupportingHalfedge::flipEdge(uint32_t edgeIdx) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& edges = intrinsicConn.getEdges();

    if (edgeIdx >= edges.size()) {
        return false;
    }

    uint32_t ha1 = edges[edgeIdx].halfEdgeIdx;
    if (ha1 == INVALID_INDEX) {
        return false;
    }

    const auto& halfedges = intrinsicConn.getHalfEdges();
    uint32_t hb1 = halfedges[ha1].opposite;
    if (hb1 == INVALID_INDEX) {
        return false;
    }

    static int flipCount = 0;
    std::unordered_set<uint32_t> affectedTriangles;

    // Update before flip
    updateRemoval(ha1);
    updateRemoval(hb1);

    // Call flip on the intrinsic mesh
    bool success = intrinsicConn.flipEdge(edgeIdx);

    if (success) {
        // Update after flip
        updateInsertion(ha1);
        updateInsertion(hb1);
    }

    return success;
}

int SupportingHalfedge::makeDelaunay(int maxIterations, std::vector<uint32_t>* flippedEdges) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    int totalFlips = 0;

    for (int iter = 0; iter < maxIterations; ++iter) {
        std::queue<uint32_t> queueEdges;
        std::unordered_set<uint32_t> inQueueEdges;

        const auto& edges = intrinsicConn.getEdges();
        for (uint32_t ei = 0; ei < edges.size(); ++ei) {
            uint32_t he = edges[ei].halfEdgeIdx;
            if (he != INVALID_INDEX && !intrinsicConn.isDelaunayEdge(he)) {
                queueEdges.push(ei);
                inQueueEdges.insert(ei);
            }
        }

        if (queueEdges.empty()) {
            break;
        }

        int flipsThisIter = 0;
        const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

        while (!queueEdges.empty()) {
            uint32_t edgeIdx = queueEdges.front();
            queueEdges.pop();
            inQueueEdges.erase(edgeIdx);

            if (edgeIdx >= edges.size()) {
                continue;
            }

            uint32_t he = edges[edgeIdx].halfEdgeIdx;
            if (he == INVALID_INDEX || intrinsicConn.isDelaunayEdge(he)) {
                continue;
            }

            uint32_t heOpp = intrinsicHalfedges[he].opposite;
            uint32_t tri0 = intrinsicHalfedges[he].face;
            uint32_t tri1 = intrinsicHalfedges[heOpp].face;

            if (flipEdge(edgeIdx)) {
                totalFlips++;
                flipsThisIter++;
                if (flippedEdges) {
                    flippedEdges->push_back(edgeIdx);
                }
            }

            // Enqueue neighboring edges
            const auto& halfedges = intrinsicConn.getHalfEdges();
            std::vector<uint32_t> neighEdges;

            // Get neighbors of the flipped edge
            if (he < halfedges.size()) {
                uint32_t hn = halfedges[he].next;
                uint32_t hp = halfedges[he].prev;
                uint32_t ho = halfedges[he].opposite;

                if (hn != INVALID_INDEX && hn < halfedges.size()) {
                    uint32_t ne = halfedges[hn].edgeIdx;
                    if (ne != INVALID_INDEX && !inQueueEdges.count(ne)) {
                        queueEdges.push(ne);
                        inQueueEdges.insert(ne);
                    }
                }
                if (hp != INVALID_INDEX && hp < halfedges.size()) {
                    uint32_t pe = halfedges[hp].edgeIdx;
                    if (pe != INVALID_INDEX && !inQueueEdges.count(pe)) {
                        queueEdges.push(pe);
                        inQueueEdges.insert(pe);
                    }
                }
                if (ho != INVALID_INDEX && ho < halfedges.size()) {
                    uint32_t hon = halfedges[ho].next;
                    uint32_t hop = halfedges[ho].prev;
                    if (hon != INVALID_INDEX && hon < halfedges.size()) {
                        uint32_t ne = halfedges[hon].edgeIdx;
                        if (ne != INVALID_INDEX && !inQueueEdges.count(ne)) {
                            queueEdges.push(ne);
                            inQueueEdges.insert(ne);
                        }
                    }
                    if (hop != INVALID_INDEX && hop < halfedges.size()) {
                        uint32_t pe = halfedges[hop].edgeIdx;
                        if (pe != INVALID_INDEX && !inQueueEdges.count(pe)) {
                            queueEdges.push(pe);
                            inQueueEdges.insert(pe);
                        }
                    }
                }
            }
        }  // end while 

        if (flipsThisIter == 0) {
            break;
        }
    }  // end for 

    // Count non zero angles after flipping
    int nonZeroCount = 0;
    for (const auto& info : supportingInfo) {
        if (info.supportingAngle != 0.0) {
            nonZeroCount++;
        }
    }
    return totalFlips;
}

const SupportingHalfedge::SupportingInfo& SupportingHalfedge::getSupportingInfo(uint32_t inputTriangleIdx) const {
    static SupportingInfo invalid;
    if (inputTriangleIdx >= supportingInfo.size()) {
        return invalid;
    }
    return supportingInfo[inputTriangleIdx];
}

void SupportingHalfedge::trackInsertedVertex(uint32_t vertexIdx, const GeodesicTracer::SurfacePoint& surfacePoint) {
    InsertedVertexLocation loc;

    switch (surfacePoint.type) {
    case GeodesicTracer::SurfacePoint::Type::VERTEX:
        loc.locationType = 0;
        break;
    case GeodesicTracer::SurfacePoint::Type::EDGE:
        loc.locationType = 1;
        break;
    case GeodesicTracer::SurfacePoint::Type::FACE:
        loc.locationType = 2;
        break;
    }

    loc.elementId = surfacePoint.elementId;

    // Convert edge split to barycentric or use face barycoords
    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        double t = surfacePoint.split;
        loc.baryCoords = glm::vec3(1.0f - static_cast<float>(t), static_cast<float>(t), 0.0f);
    }
    else {
        loc.baryCoords = glm::vec3(
            static_cast<float>(surfacePoint.baryCoords.x),
            static_cast<float>(surfacePoint.baryCoords.y),
            static_cast<float>(surfacePoint.baryCoords.z)
        );
    }

    insertedVertexLocations[vertexIdx] = loc;

    //std::cout << "[SupportingHalfedge] Tracked inserted vertex " << vertexIdx  << " at type=" << loc.locationType << " elem=" << loc.elementId << std::endl;
}

SupportingHalfedge::IntrinsicMesh SupportingHalfedge::buildIntrinsicMesh() const {
    // Return cached mesh if valid
    if (intrinsicMeshCacheValid) {
        return cachedIntrinsicMesh;
    }

    std::cout << "[SupportingHalfedge] Building intrinsic mesh (cache miss)..." << std::endl;

    IntrinsicMesh meshData;

    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicVertices = intrinsicConn.getVertices();
    const auto& intrinsicFaces = intrinsicConn.getFaces();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();
    const auto& inputVertices = inputMesh.getConnectivity().getVertices();

    // Build deduplicated vertex buffer
    meshData.vertices.reserve(intrinsicVertices.size());

    for (uint32_t vertIdx = 0; vertIdx < intrinsicVertices.size(); ++vertIdx) {
        IntrinsicVertex vertex;
        vertex.intrinsicVertexId = vertIdx;
        vertex.position = intrinsicVertices[vertIdx].position;

        if (vertIdx < inputVertices.size()) {
            // Original vertex from input mesh
            vertex.inputLocationType = 0; // VERTEX
            vertex.inputElementId = vertIdx;
            vertex.inputBaryCoords = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        else {
            // Inserted vertex
            auto it = insertedVertexLocations.find(vertIdx);
            if (it != insertedVertexLocations.end()) {
                const auto& loc = it->second;
                vertex.inputLocationType = loc.locationType;
                vertex.inputElementId = loc.elementId;
                vertex.inputBaryCoords = loc.baryCoords;
            }
        }
        meshData.vertices.push_back(vertex);
    }

    // Build index buffer and face ID mapping
    meshData.indices.reserve(intrinsicFaces.size() * 3);
    meshData.faceIds.reserve(intrinsicFaces.size());
    meshData.triangles.reserve(intrinsicFaces.size());

    for (uint32_t faceIdx = 0; faceIdx < intrinsicFaces.size(); ++faceIdx) {
        const auto& face = intrinsicFaces[faceIdx];
        if (face.halfEdgeIdx == INVALID_INDEX) {
            continue;
        }

        // Get the three vertices of this face
        uint32_t he0 = face.halfEdgeIdx;
        uint32_t he1 = intrinsicHalfedges[he0].next;
        uint32_t he2 = intrinsicHalfedges[he1].next;

        uint32_t v0 = intrinsicHalfedges[he0].origin;
        uint32_t v1 = intrinsicHalfedges[he1].origin;
        uint32_t v2 = intrinsicHalfedges[he2].origin;

        // Add indices for this triangle
        meshData.indices.push_back(v0);
        meshData.indices.push_back(v1);
        meshData.indices.push_back(v2);

        // Store intrinsic face ID
        meshData.faceIds.push_back(faceIdx);

        // Calculate triangle geometric data
        glm::vec3 p0 = meshData.vertices[v0].position;
        glm::vec3 p1 = meshData.vertices[v1].position;
        glm::vec3 p2 = meshData.vertices[v2].position;

        // Calculate edges
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;

        // Calculate normal
        glm::vec3 crossProd = glm::cross(edge1, edge2);
        float crossLength = glm::length(crossProd);

        IntrinsicTriangle tri;
        tri.vertexIndices[0] = v0;
        tri.vertexIndices[1] = v1;
        tri.vertexIndices[2] = v2;
        tri.faceId = faceIdx;
        tri.center = (p0 + p1 + p2) / 3.0f;
        tri.area = crossLength * 0.5f;
        tri.normal = (crossLength > 1e-8f) ? (crossProd / crossLength) : glm::vec3(0.0f, 1.0f, 0.0f);

        meshData.triangles.push_back(tri);
    }

    // Calculate area weighted vertex normals from neighboring triangles
    calculateVertexNormals(meshData);

    std::cout << "[SupportingHalfedge] Built intrinsic mesh GPU data:" << std::endl;
    std::cout << "  Vertices: " << meshData.vertices.size() << std::endl;
    std::cout << "  Triangles: " << meshData.triangles.size() << std::endl;
    std::cout << "  Indices: " << meshData.indices.size() << std::endl;
    std::cout << "  Total area: " << std::accumulate(meshData.triangles.begin(), meshData.triangles.end(), 0.0f,
        [](float sum, const IntrinsicTriangle& t) { return sum + t.area; }) << " m^2" << std::endl;

    // Cache the result
    cachedIntrinsicMesh = meshData;
    intrinsicMeshCacheValid = true;

    return meshData;
}

void SupportingHalfedge::calculateVertexNormals(IntrinsicMesh& meshData) const {
    // Initialize all vertex normals to zero
    for (auto& vertex : meshData.vertices) {
        vertex.normal = glm::vec3(0.0f);
    }

    // Build a map of vertex to triangles that use it
    std::unordered_map<uint32_t, std::vector<uint32_t>> vertexToTriangles;
    for (uint32_t triIdx = 0; triIdx < meshData.triangles.size(); ++triIdx) {
        const auto& tri = meshData.triangles[triIdx];
        for (int i = 0; i < 3; ++i) {
            uint32_t vertexIdx = tri.vertexIndices[i];
            vertexToTriangles[vertexIdx].push_back(triIdx);
        }
    }

    // Calculate area weighted normals for each vertex
    for (auto& vertex : meshData.vertices) {
        uint32_t vertexIdx = vertex.intrinsicVertexId;
        
        auto it = vertexToTriangles.find(vertexIdx);
        if (it == vertexToTriangles.end()) {
            // Vertex not used by any triangle, default to up
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            continue;
        }

        glm::vec3 areaWeightedNormal(0.0f);
        float totalArea = 0.0f;

        // Accumulate area weighted normals from all neighboring triangles
        for (uint32_t triIdx : it->second) {
            const auto& tri = meshData.triangles[triIdx];
            
            // Weight by triangle area to evenly interpolate
            areaWeightedNormal += tri.normal * tri.area;
            totalArea += tri.area;
        }

        // Normalize the accumulated normal
        vertex.normal = glm::normalize(areaWeightedNormal / totalArea);
    }
}

SupportingHalfedge::GPUBuffers SupportingHalfedge::buildGPUBuffers() const {
    GPUBuffers buffers;

    const auto& inputConn = inputMesh.getConnectivity();
    auto& intrinsicConn = intrinsicMesh.getConnectivity();

    const auto& inputFaces = inputConn.getFaces();
    const auto& inputHalfedges = inputConn.getHalfEdges();

    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();
    const auto& intrinsicEdges = intrinsicConn.getEdges();
    const auto& intrinsicFaces = intrinsicConn.getFaces();

    // Build S[] and A[] - one entry per input triangle
    buffers.S.resize(inputFaces.size());
    buffers.A.resize(inputFaces.size());

    for (size_t faceIdx = 0; faceIdx < inputFaces.size(); ++faceIdx) {
        const auto& face = inputFaces[faceIdx];
        if (face.halfEdgeIdx == INVALID_INDEX) {
            buffers.S[faceIdx] = -1;
            buffers.A[faceIdx] = 0.0f;
            continue;
        }

        // supportingInfo is indexed by faceIdx
        if (faceIdx < supportingInfo.size()) {
            buffers.S[faceIdx] = static_cast<int32_t>(supportingInfo[faceIdx].supportingHE);
            buffers.A[faceIdx] = static_cast<float>(supportingInfo[faceIdx].supportingAngle);
        }
        else {
            buffers.S[faceIdx] = -1;
            buffers.A[faceIdx] = 0.0f;
        }
    }

    // Build H[] - intrinsic halfedge data [origin, edge, face, next] (4 ints per halfedge)
    buffers.H.reserve(intrinsicHalfedges.size() * 4);
    for (const auto& he : intrinsicHalfedges) {
        buffers.H.push_back(static_cast<int32_t>(he.origin));
        buffers.H.push_back(static_cast<int32_t>(he.edgeIdx));
        buffers.H.push_back(static_cast<int32_t>(he.face));
        buffers.H.push_back(static_cast<int32_t>(he.next));
    }

    // Build E[] - intrinsic edge data [he0, he1] (2 ints per edge)
    buffers.E.reserve(intrinsicEdges.size() * 2);
    for (const auto& edge : intrinsicEdges) {
        buffers.E.push_back(static_cast<int32_t>(edge.halfEdgeIdx));
        if (edge.halfEdgeIdx != INVALID_INDEX && edge.halfEdgeIdx < intrinsicHalfedges.size()) {
            buffers.E.push_back(static_cast<int32_t>(intrinsicHalfedges[edge.halfEdgeIdx].opposite));
        }
        else {
            buffers.E.push_back(-1);
        }
    }

    // Build T[] - intrinsic triangle data [halfedge] (1 int per triangle)
    buffers.T.reserve(intrinsicFaces.size());
    for (const auto& face : intrinsicFaces) {
        buffers.T.push_back(static_cast<int32_t>(face.halfEdgeIdx));
    }

    // Build L[] - intrinsic edge lengths (1 float per edge)
    buffers.L.reserve(intrinsicEdges.size());
    for (const auto& edge : intrinsicEdges) {
        buffers.L.push_back(static_cast<float>(edge.intrinsicLength));
    }

    // Build input mesh buffers 
    const auto& inputEdges = inputConn.getEdges();

    // Build H_input[] - input halfedge data
    buffers.H_input.reserve(inputHalfedges.size() * 4);
    for (const auto& he : inputHalfedges) {
        buffers.H_input.push_back(static_cast<int32_t>(he.origin));
        buffers.H_input.push_back(static_cast<int32_t>(he.edgeIdx));
        buffers.H_input.push_back(static_cast<int32_t>(he.face));
        buffers.H_input.push_back(static_cast<int32_t>(he.next));
    }

    // Build E_input[] - input edge data
    buffers.E_input.reserve(inputEdges.size() * 2);
    for (const auto& edge : inputEdges) {
        buffers.E_input.push_back(static_cast<int32_t>(edge.halfEdgeIdx));
        if (edge.halfEdgeIdx != INVALID_INDEX && edge.halfEdgeIdx < inputHalfedges.size()) {
            buffers.E_input.push_back(static_cast<int32_t>(inputHalfedges[edge.halfEdgeIdx].opposite));
        }
        else {
            buffers.E_input.push_back(-1);
        }
    }

    // Build T_input[] - input triangle data
    buffers.T_input.reserve(inputFaces.size());
    for (const auto& face : inputFaces) {
        buffers.T_input.push_back(static_cast<int32_t>(face.halfEdgeIdx));
    }

    // Build L_input[] - input edge lengths
    buffers.L_input.reserve(inputEdges.size());
    for (const auto& edge : inputEdges) {
        buffers.L_input.push_back(static_cast<float>(edge.intrinsicLength));
    }
    return buffers;
}

void SupportingHalfedge::uploadToGPU() {
    std::cout << "[SupportingHalfedge] Uploading data to GPU..." << std::endl;

    if (gpuDataUploaded) {
        cleanup();
    }

    // Build CPU side buffers
    auto gpuBuffers = buildGPUBuffers();

    // Upload S[] - supporting halfedge 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.S.data(), gpuBuffers.S.size() * sizeof(int32_t),
        VK_FORMAT_R32_SINT,
        bufferS, offsetS, bufferViewS
    );

    // Upload A[] - supporting angle 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.A.data(), gpuBuffers.A.size() * sizeof(float),
        VK_FORMAT_R32_SFLOAT,
        bufferA, offsetA, bufferViewA
    );

    // Upload H[] - halfedge data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.H.data(), gpuBuffers.H.size() * sizeof(int32_t),
        VK_FORMAT_R32G32B32A32_SINT,
        bufferH, offsetH, bufferViewH
    );

    // Upload E[] - edge data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.E.data(), gpuBuffers.E.size() * sizeof(int32_t),
        VK_FORMAT_R32G32_SINT,
        bufferE, offsetE, bufferViewE
    );

    // Upload T[] - triangle data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.T.data(), gpuBuffers.T.size() * sizeof(int32_t),
        VK_FORMAT_R32_SINT,
        bufferT, offsetT, bufferViewT
    );

    // Upload L[] - edge lengths 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.L.data(), gpuBuffers.L.size() * sizeof(float),
        VK_FORMAT_R32_SFLOAT,
        bufferL, offsetL, bufferViewL
    );

    // Upload input mesh buffers
    // Upload H_input[] - input halfedge data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.H_input.data(), gpuBuffers.H_input.size() * sizeof(int32_t),
        VK_FORMAT_R32G32B32A32_SINT,
        bufferH_input, offsetH_input, bufferViewH_input
    );

    // Upload E_input[] - input edge data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.E_input.data(), gpuBuffers.E_input.size() * sizeof(int32_t),
        VK_FORMAT_R32G32_SINT,
        bufferE_input, offsetE_input, bufferViewE_input
    );

    // Upload T_input[] - input triangle data 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.T_input.data(), gpuBuffers.T_input.size() * sizeof(int32_t),
        VK_FORMAT_R32_SINT,
        bufferT_input, offsetT_input, bufferViewT_input
    );

    // Upload L_input[] - input edge lengths 
    createTexelBuffer(
        allocator, vulkanDevice,
        gpuBuffers.L_input.data(), gpuBuffers.L_input.size() * sizeof(float),
        VK_FORMAT_R32_SFLOAT,
        bufferL_input, offsetL_input, bufferViewL_input
    );

    gpuDataUploaded = true;
}

void SupportingHalfedge::uploadIntrinsicTriangleData() {
    auto intrinsicMeshData = buildIntrinsicMesh();

    if (!intrinsicMeshData.triangles.empty()) {
        // Convert to GPU format
        std::vector<IntrinsicTriangleData> gpuTriangles;
        gpuTriangles.reserve(intrinsicMeshData.triangles.size());

        for (const auto& tri : intrinsicMeshData.triangles) {
            IntrinsicTriangleData gpuTri;
            gpuTri.center = tri.center;
            gpuTri.normal = tri.normal;
            gpuTri.area = tri.area;
            gpuTri.padding = 0.0f;
            gpuTriangles.push_back(gpuTri);
        }

        // Clean up existing buffer if any
        if (intrinsicTriangleBuffer != VK_NULL_HANDLE) {
            allocator.free(intrinsicTriangleBuffer, triangleGeometryOffset);
            intrinsicTriangleBuffer = VK_NULL_HANDLE;
        }

        // Upload to GPU as storage buffer
        void* mappedPtr;
        createStorageBuffer(
            allocator, vulkanDevice,
            gpuTriangles.data(), gpuTriangles.size() * sizeof(IntrinsicTriangleData),
            intrinsicTriangleBuffer, triangleGeometryOffset, &mappedPtr
        );

        std::cout << "[SupportingHalfedge] Uploaded " << gpuTriangles.size() << " triangle geometries to GPU" << std::endl;
    }
}

void SupportingHalfedge::uploadIntrinsicVertexData() {
    auto intrinsicMeshData = buildIntrinsicMesh();

    if (!intrinsicMeshData.vertices.empty()) {
        // Convert to GPU format
        std::vector<IntrinsicVertexData> gpuVertices;
        gpuVertices.reserve(intrinsicMeshData.vertices.size());

        for (const auto& vertex : intrinsicMeshData.vertices) {
            IntrinsicVertexData gpuVertex;
            gpuVertex.position = vertex.position;
            gpuVertex.intrinsicVertexId = vertex.intrinsicVertexId;
            gpuVertex.normal = vertex.normal;
            gpuVertex.padding = 0.0f;
            gpuVertices.push_back(gpuVertex);
        }

        // Clean up existing buffer if any
        if (intrinsicVertexBuffer != VK_NULL_HANDLE) {
            allocator.free(intrinsicVertexBuffer, vertexGeometryOffset);
            intrinsicVertexBuffer = VK_NULL_HANDLE;
        }

        // Upload to GPU as storage buffer
        void* mappedPtr;
        createStorageBuffer(
            allocator, vulkanDevice,
            gpuVertices.data(), gpuVertices.size() * sizeof(IntrinsicVertexData),
            intrinsicVertexBuffer, vertexGeometryOffset, &mappedPtr
        );

        std::cout << "[SupportingHalfedge] Uploaded " << gpuVertices.size() << " vertex geometries to GPU" << std::endl;
    }
}

float SupportingHalfedge::getAverageTriangleArea() const {
    if (!intrinsicMeshCacheValid) {
        buildIntrinsicMesh();
    }
    
    if (cachedIntrinsicMesh.triangles.empty()) {
        return 0.0f;
    }
    
    float totalArea = 0.0f;
    for (const auto& tri : cachedIntrinsicMesh.triangles) {
        totalArea += tri.area;
    }
    
    return totalArea / static_cast<float>(cachedIntrinsicMesh.triangles.size());
}

double SupportingHalfedge::getIntrinsicCornerAngle(uint32_t intrinsicHE) const {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

    if (intrinsicHE >= intrinsicHalfedges.size()) {
        return 0.0;
    }

    double storedAngle = intrinsicHalfedges[intrinsicHE].cornerAngle;

    if (storedAngle > 0.0) {
        return storedAngle;
    }
}

void SupportingHalfedge::cleanup() {
    if (bufferViewS != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewS, nullptr);
        bufferViewS = VK_NULL_HANDLE;
    }
    if (bufferViewA != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewA, nullptr);
        bufferViewA = VK_NULL_HANDLE;
    }
    if (bufferViewH != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewH, nullptr);
        bufferViewH = VK_NULL_HANDLE;
    }
    if (bufferViewE != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewE, nullptr);
        bufferViewE = VK_NULL_HANDLE;
    }
    if (bufferViewT != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewT, nullptr);
        bufferViewT = VK_NULL_HANDLE;
    }
    if (bufferViewL != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewL, nullptr);
        bufferViewL = VK_NULL_HANDLE;
    }

    if (bufferViewH_input != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewH_input, nullptr);
        bufferViewH_input = VK_NULL_HANDLE;
    }
    if (bufferViewE_input != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewE_input, nullptr);
        bufferViewE_input = VK_NULL_HANDLE;
    }
    if (bufferViewT_input != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewT_input, nullptr);
        bufferViewT_input = VK_NULL_HANDLE;
    }
    if (bufferViewL_input != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), bufferViewL_input, nullptr);
        bufferViewL_input = VK_NULL_HANDLE;
    }

    if (bufferS != VK_NULL_HANDLE) {
        allocator.free(bufferS, offsetS);
        bufferS = VK_NULL_HANDLE;
    }
    if (bufferA != VK_NULL_HANDLE) {
        allocator.free(bufferA, offsetA);
        bufferA = VK_NULL_HANDLE;
    }
    if (bufferH != VK_NULL_HANDLE) {
        allocator.free(bufferH, offsetH);
        bufferH = VK_NULL_HANDLE;
    }
    if (bufferE != VK_NULL_HANDLE) {
        allocator.free(bufferE, offsetE);
        bufferE = VK_NULL_HANDLE;
    }
    if (bufferT != VK_NULL_HANDLE) {
        allocator.free(bufferT, offsetT);
        bufferT = VK_NULL_HANDLE;
    }
    if (bufferL != VK_NULL_HANDLE) {
        allocator.free(bufferL, offsetL);
        bufferL = VK_NULL_HANDLE;
    }

    if (bufferH_input != VK_NULL_HANDLE) {
        allocator.free(bufferH_input, offsetH_input);
        bufferH_input = VK_NULL_HANDLE;
    }
    if (bufferE_input != VK_NULL_HANDLE) {
        allocator.free(bufferE_input, offsetE_input);
        bufferE_input = VK_NULL_HANDLE;
    }
    if (bufferT_input != VK_NULL_HANDLE) {
        allocator.free(bufferT_input, offsetT_input);
        bufferT_input = VK_NULL_HANDLE;
    }
    if (bufferL_input != VK_NULL_HANDLE) {
        allocator.free(bufferL_input, offsetL_input);
        bufferL_input = VK_NULL_HANDLE;
    }

    if (intrinsicTriangleBuffer != VK_NULL_HANDLE) {
        allocator.free(intrinsicTriangleBuffer, triangleGeometryOffset);
        intrinsicTriangleBuffer = VK_NULL_HANDLE;
    }

    if (intrinsicVertexBuffer != VK_NULL_HANDLE) {
        allocator.free(intrinsicVertexBuffer, vertexGeometryOffset);
        intrinsicVertexBuffer = VK_NULL_HANDLE;
    }

    gpuDataUploaded = false;
}