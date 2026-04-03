#include "SupportingHalfedge.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <map>
#include <queue>
#include <set>
#include <unordered_set>

SupportingHalfedge::SupportingHalfedge(const SignpostMesh& inputMesh, SignpostMesh& intrinsicMesh, const GeodesicTracer& tracer)
    : inputMesh(inputMesh)
    , intrinsicMesh(intrinsicMesh)
    , tracer(tracer)
{
}

SupportingHalfedge::~SupportingHalfedge() = default;

void SupportingHalfedge::initialize() {
    const auto& inputConn = inputMesh.getConnectivity();
    const auto& inputFaces = inputConn.getFaces();
    const auto& inputHalfedges = inputConn.getHalfEdges();

    const auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicFaces = intrinsicConn.getFaces();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();

    supportingInfoPerHalfedge.resize(inputHalfedges.size());
    for (auto& info : supportingInfoPerHalfedge) {
        info.supportingHE = INVALID_INDEX;
        info.supportingAngle = 0.0;
    }

    for (size_t faceIdx = 0; faceIdx < inputFaces.size(); ++faceIdx) {
        if (faceIdx >= intrinsicFaces.size()) {
            continue;
        }

        uint32_t inputHe = inputFaces[faceIdx].halfEdgeIdx;
        uint32_t intrinsicHe = intrinsicFaces[faceIdx].halfEdgeIdx;
        for (int i = 0; i < 3; ++i) {
            if (inputHe == INVALID_INDEX || inputHe >= inputHalfedges.size() ||
                intrinsicHe == INVALID_INDEX || intrinsicHe >= intrinsicHalfedges.size()) {
                break;
            }

            supportingInfoPerHalfedge[inputHe].supportingHE = intrinsicHe;
            supportingInfoPerHalfedge[inputHe].supportingAngle = 0.0;

            inputHe = inputHalfedges[inputHe].next;
            intrinsicHe = intrinsicHalfedges[intrinsicHe].next;
        }
    }

}

void SupportingHalfedge::updateRemoval(uint32_t intrinsicHE) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();
    const auto& intrinsicFaces = intrinsicConn.getFaces();

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

    auto cornerAngleAtHalfedge = [&](uint32_t he) -> double {
        if (he == INVALID_INDEX || he >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t faceIdx = intrinsicHalfedges[he].face;
        if (faceIdx == INVALID_INDEX || faceIdx >= intrinsicFaces.size()) {
            return 0.0;
        }

        const uint32_t he0 = intrinsicFaces[faceIdx].halfEdgeIdx;
        if (he0 == INVALID_INDEX || he0 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t he1 = intrinsicHalfedges[he0].next;
        if (he1 == INVALID_INDEX || he1 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t he2 = intrinsicHalfedges[he1].next;
        if (he2 == INVALID_INDEX || he2 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const auto angles = intrinsicMesh.computeCornerAngles(faceIdx);
        if (he == he0) {
            return angles[0];
        }
        if (he == he1) {
            return angles[1];
        }
        if (he == he2) {
            return angles[2];
        }

        return 0.0;
    };

    for (uint32_t he : helist) {
        if (he >= inputHalfedges.size()) {
            continue;
        }

        if (he >= supportingInfoPerHalfedge.size()) {
            continue;
        }

        SupportingInfo& info = supportingInfoPerHalfedge[he];
        if (info.supportingHE == intrinsicHE) {
            uint32_t mate_h = intrinsicHalfedges[intrinsicHE].opposite;
            if (mate_h == INVALID_INDEX || mate_h >= intrinsicHalfedges.size()) {
                continue;
            }
            uint32_t new_ref = intrinsicHalfedges[mate_h].next;
            if (new_ref == INVALID_INDEX || new_ref >= intrinsicHalfedges.size()) {
                continue;
            }

            info.supportingHE = new_ref;

            double theta = cornerAngleAtHalfedge(new_ref);
            info.supportingAngle -= theta;
            clampSupportingAngle(info);
        }
    }

}

void SupportingHalfedge::updateInsertion(uint32_t h) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();
    const auto& intrinsicFaces = intrinsicConn.getFaces();

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

    auto cornerAngleAtHalfedge = [&](uint32_t he) -> double {
        if (he == INVALID_INDEX || he >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t faceIdx = intrinsicHalfedges[he].face;
        if (faceIdx == INVALID_INDEX || faceIdx >= intrinsicFaces.size()) {
            return 0.0;
        }

        const uint32_t he0 = intrinsicFaces[faceIdx].halfEdgeIdx;
        if (he0 == INVALID_INDEX || he0 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t he1 = intrinsicHalfedges[he0].next;
        if (he1 == INVALID_INDEX || he1 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const uint32_t he2 = intrinsicHalfedges[he1].next;
        if (he2 == INVALID_INDEX || he2 >= intrinsicHalfedges.size()) {
            return 0.0;
        }

        const auto angles = intrinsicMesh.computeCornerAngles(faceIdx);
        if (he == he0) {
            return angles[0];
        }
        if (he == he1) {
            return angles[1];
        }
        if (he == he2) {
            return angles[2];
        }

        return 0.0;
    };

    for (uint32_t he : helist) {
        if (he >= inputHalfedges.size()) {
            continue;
        }

        if (he >= supportingInfoPerHalfedge.size()) {
            continue;
        }

        SupportingInfo& info = supportingInfoPerHalfedge[he];
        uint32_t ref = info.supportingHE;
        if (ref == INVALID_INDEX || ref >= intrinsicHalfedges.size()) {
            continue;
        }

        uint32_t prevRef = intrinsicHalfedges[ref].prev;
        if (prevRef == INVALID_INDEX || prevRef >= intrinsicHalfedges.size()) {
            continue;
        }

        uint32_t matePrev = intrinsicHalfedges[prevRef].opposite;

        if (matePrev == h) {
            double theta = cornerAngleAtHalfedge(ref);

            double newAngle = info.supportingAngle + theta;

            if (newAngle <= 0.0) {
                info.supportingHE = h;

                info.supportingAngle = newAngle;
                clampSupportingAngle(info);
            }
        }
    }

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
    const auto& edges = intrinsicConn.getEdges();
    std::vector<uint32_t> allEdges;
    allEdges.reserve(edges.size());
    for (uint32_t edgeIdx = 0; edgeIdx < edges.size(); ++edgeIdx) {
        if (edges[edgeIdx].halfEdgeIdx != INVALID_INDEX) {
            allEdges.push_back(edgeIdx);
        }
    }

    return makeDelaunayLocal(maxIterations, allEdges, flippedEdges);
}

int SupportingHalfedge::makeDelaunayLocal(int maxIterations, const std::vector<uint32_t>& seedEdges, std::vector<uint32_t>* flippedEdges) {
    auto& intrinsicConn = intrinsicMesh.getConnectivity();
    int totalFlips = 0;

    for (int iter = 0; iter < maxIterations; ++iter) {
        std::queue<uint32_t> queueEdges;
        std::unordered_set<uint32_t> inQueueEdges;

        const auto& edges = intrinsicConn.getEdges();
        for (uint32_t edgeIdx : seedEdges) {
            if (edgeIdx >= edges.size()) {
                continue;
            }

            uint32_t he = edges[edgeIdx].halfEdgeIdx;
            if (he != INVALID_INDEX && !intrinsicConn.isDelaunayEdge(he) && !inQueueEdges.count(edgeIdx)) {
                queueEdges.push(edgeIdx);
                inQueueEdges.insert(edgeIdx);
            }
        }

        if (queueEdges.empty()) {
            break;
        }

        int flipsThisIter = 0;

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

            if (flipEdge(edgeIdx)) {
                totalFlips++;
                flipsThisIter++;
                if (flippedEdges) {
                    flippedEdges->push_back(edgeIdx);
                }
                for (uint32_t nhe : intrinsicConn.getNeighboringHalfEdges(he)) {
                    uint32_t neighEdgeIdx = intrinsicConn.getEdgeFromHalfEdge(nhe);
                    if (neighEdgeIdx != INVALID_INDEX && !inQueueEdges.count(neighEdgeIdx)) {
                        queueEdges.push(neighEdgeIdx);
                        inQueueEdges.insert(neighEdgeIdx);
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
    for (const auto& info : supportingInfoPerHalfedge) {
        if (info.supportingAngle != 0.0) {
            nonZeroCount++;
        }
    }
    return totalFlips;
}

void SupportingHalfedge::clampSupportingAngle(SupportingInfo& info) const {
    const double pi = glm::pi<double>();
    const double eps = 1e-12;

    if (info.supportingAngle > 0.0 && info.supportingAngle <= eps) {
        info.supportingAngle = 0.0;
    }
    if (info.supportingAngle <= -pi && info.supportingAngle > -pi - eps) {
        info.supportingAngle = -pi + eps;
    }
}

const SupportingHalfedge::SupportingInfo& SupportingHalfedge::getSupportingInfo(uint32_t inputHalfedgeIdx) const {
    static SupportingInfo invalid;
    if (inputHalfedgeIdx >= supportingInfoPerHalfedge.size()) {
        return invalid;
    }
    return supportingInfoPerHalfedge[inputHalfedgeIdx];
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
}

SupportingHalfedge::IntrinsicMesh SupportingHalfedge::buildIntrinsicMesh() const {
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
    const auto& inputHalfedges = inputConn.getHalfEdges();
    const auto& inputFaces = inputConn.getFaces();
    const auto& inputEdges = inputConn.getEdges();
    const auto& intrinsicHalfedges = intrinsicConn.getHalfEdges();
    const auto& intrinsicEdges = intrinsicConn.getEdges();
    const auto& intrinsicFaces = intrinsicConn.getFaces();

    buffers.supportingHalfedges.resize(inputHalfedges.size(), -1);
    buffers.supportingAngles.resize(inputHalfedges.size(), 0.0f);
    for (size_t inputHe = 0; inputHe < inputHalfedges.size() && inputHe < supportingInfoPerHalfedge.size(); ++inputHe) {
        const SupportingInfo& info = supportingInfoPerHalfedge[inputHe];
        buffers.supportingHalfedges[inputHe] = static_cast<int32_t>(info.supportingHE);
        buffers.supportingAngles[inputHe] = static_cast<float>(info.supportingAngle);
    }

    buffers.intrinsicHalfedgeData.reserve(intrinsicHalfedges.size() * 4);
    for (const auto& he : intrinsicHalfedges) {
        buffers.intrinsicHalfedgeData.push_back(static_cast<int32_t>(he.origin));
        buffers.intrinsicHalfedgeData.push_back(static_cast<int32_t>(he.edgeIdx));
        buffers.intrinsicHalfedgeData.push_back(static_cast<int32_t>(he.face));
        buffers.intrinsicHalfedgeData.push_back(static_cast<int32_t>(he.next));
    }

    buffers.intrinsicEdgeData.reserve(intrinsicEdges.size() * 2);
    for (const auto& edge : intrinsicEdges) {
        buffers.intrinsicEdgeData.push_back(static_cast<int32_t>(edge.halfEdgeIdx));
        if (edge.halfEdgeIdx != INVALID_INDEX && edge.halfEdgeIdx < intrinsicHalfedges.size()) {
            buffers.intrinsicEdgeData.push_back(static_cast<int32_t>(intrinsicHalfedges[edge.halfEdgeIdx].opposite));
        } else {
            buffers.intrinsicEdgeData.push_back(-1);
        }
    }

    buffers.intrinsicTriangleData.reserve(intrinsicFaces.size());
    for (const auto& face : intrinsicFaces) {
        buffers.intrinsicTriangleData.push_back(static_cast<int32_t>(face.halfEdgeIdx));
    }

    buffers.intrinsicLengths.reserve(intrinsicEdges.size());
    for (const auto& edge : intrinsicEdges) {
        buffers.intrinsicLengths.push_back(static_cast<float>(edge.intrinsicLength));
    }

    buffers.inputHalfedgeData.reserve(inputHalfedges.size() * 4);
    for (const auto& he : inputHalfedges) {
        buffers.inputHalfedgeData.push_back(static_cast<int32_t>(he.origin));
        buffers.inputHalfedgeData.push_back(static_cast<int32_t>(he.edgeIdx));
        buffers.inputHalfedgeData.push_back(static_cast<int32_t>(he.face));
        buffers.inputHalfedgeData.push_back(static_cast<int32_t>(he.next));
    }

    buffers.inputEdgeData.reserve(inputEdges.size() * 2);
    for (const auto& edge : inputEdges) {
        buffers.inputEdgeData.push_back(static_cast<int32_t>(edge.halfEdgeIdx));
        if (edge.halfEdgeIdx != INVALID_INDEX && edge.halfEdgeIdx < inputHalfedges.size()) {
            buffers.inputEdgeData.push_back(static_cast<int32_t>(inputHalfedges[edge.halfEdgeIdx].opposite));
        } else {
            buffers.inputEdgeData.push_back(-1);
        }
    }

    buffers.inputTriangleData.reserve(inputFaces.size());
    for (const auto& face : inputFaces) {
        buffers.inputTriangleData.push_back(static_cast<int32_t>(face.halfEdgeIdx));
    }

    buffers.inputLengths.reserve(inputEdges.size());
    for (const auto& edge : inputEdges) {
        buffers.inputLengths.push_back(static_cast<float>(edge.intrinsicLength));
    }

    return buffers;
}

