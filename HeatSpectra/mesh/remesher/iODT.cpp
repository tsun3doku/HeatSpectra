#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>

#include <iostream>
#include <fstream>
#include <cmath>
#include <memory>
#include <algorithm>
#include <limits>
#include <string>
#include <thread>
#include <future>
#include <deque>
#include <iomanip>

#include "iODT.hpp"

namespace {

bool isFixedRefinementEdge(const HalfEdgeMesh& conn, uint32_t edgeIdx) {
    const auto& edges = conn.getEdges();
    const auto& halfEdges = conn.getHalfEdges();

    if (edgeIdx == HalfEdgeMesh::INVALID_INDEX || edgeIdx >= edges.size()) {
        return true;
    }

    uint32_t he = edges[edgeIdx].halfEdgeIdx;
    if (he == HalfEdgeMesh::INVALID_INDEX || he >= halfEdges.size()) {
        return true;
    }

    return halfEdges[he].opposite == HalfEdgeMesh::INVALID_INDEX ||
           !conn.isFlippableEdge(edgeIdx);
}

bool isFixedRefinementHalfedge(const HalfEdgeMesh& conn, uint32_t heIdx) {
    return isFixedRefinementEdge(conn, conn.getEdgeFromHalfEdge(heIdx));
}

uint32_t previousAroundVertex(const HalfEdgeMesh& conn, uint32_t heIdx) {
    const auto& halfEdges = conn.getHalfEdges();

    if (heIdx == HalfEdgeMesh::INVALID_INDEX || heIdx >= halfEdges.size()) {
        return HalfEdgeMesh::INVALID_INDEX;
    }

    uint32_t twin = halfEdges[heIdx].opposite;
    if (twin == HalfEdgeMesh::INVALID_INDEX || twin >= halfEdges.size()) {
        return HalfEdgeMesh::INVALID_INDEX;
    }

    uint32_t next = halfEdges[twin].next;
    if (next == HalfEdgeMesh::INVALID_INDEX || next >= halfEdges.size()) {
        return HalfEdgeMesh::INVALID_INDEX;
    }

    return next;
}

size_t countNeedleCornersGC(SignpostMesh& mesh, uint32_t faceIdx) {
    auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    std::vector<uint32_t> faceHEs = conn.getFaceHalfEdges(faceIdx);
    size_t needleCorners = 0;

    for (uint32_t he : faceHEs) {
        if (he == HalfEdgeMesh::INVALID_INDEX || he >= halfEdges.size()) {
            continue;
        }

        double angleSum = 0.0;
        uint32_t curr = he;
        bool validWalk = true;

        for (size_t step = 0; step < halfEdges.size(); ++step) {
            if (curr == HalfEdgeMesh::INVALID_INDEX || curr >= halfEdges.size()) {
                validWalk = false;
                break;
            }

            angleSum += mesh.getCornerAngle(curr);

            uint32_t next = conn.getNextAroundVertex(curr);
            if (next == HalfEdgeMesh::INVALID_INDEX || next >= halfEdges.size()) {
                validWalk = false;
                break;
            }

            curr = next;
            if (curr == he || isFixedRefinementHalfedge(conn, curr)) {
                break;
            }
        }

        if (!validWalk) {
            continue;
        }

        if (curr != he && !isFixedRefinementHalfedge(conn, he)) {
            curr = he;
            for (size_t step = 0; step < halfEdges.size(); ++step) {
                curr = previousAroundVertex(conn, curr);
                if (curr == HalfEdgeMesh::INVALID_INDEX || curr >= halfEdges.size()) {
                    validWalk = false;
                    break;
                }

                angleSum += mesh.getCornerAngle(curr);

                if (isFixedRefinementHalfedge(conn, curr)) {
                    break;
                }
            }
        }

        if (validWalk && angleSum < glm::pi<double>() / 3.0) {
            ++needleCorners;
        }
    }

    return needleCorners;
}

} // namespace

iODT::iODT(const std::vector<float>& pointPositions, const std::vector<uint32_t>& triangleIndices)
    : tracer(intrinsicMesh), tracerInput(inputMesh) {

    inputMesh.buildFromIndexedData(pointPositions, triangleIndices);
    inputMesh.updateAllCornerAngles({});
    inputMesh.computeCornerScaledAngles();
    inputMesh.updateAllSignposts();
    inputMesh.computeVertexAngleScales();
    inputMesh.buildHalfedgeVectorsInVertex();
    inputMesh.buildHalfedgeVectorsInFace();

    intrinsicMesh.buildFromIndexedData(pointPositions, triangleIndices);
    auto& conn = intrinsicMesh.getConnectivity();
    intrinsicMesh.updateAllCornerAngles({});
    intrinsicMesh.computeCornerScaledAngles();
    intrinsicMesh.updateAllSignposts();
    intrinsicMesh.computeVertexAngleScales();
    intrinsicMesh.buildHalfedgeVectorsInVertex();
    intrinsicMesh.buildHalfedgeVectorsInFace();

    initializeVertexLocations();

    const auto& edges = conn.getEdges();
    for (size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].halfEdgeIdx != INVALID_INDEX) {
            conn.getEdges()[i].isOriginal = true;
        }
    }

    supportingHalfedge = std::make_unique<SupportingHalfedge>(inputMesh, intrinsicMesh, tracer);
    supportingHalfedge->initialize();
}

iODT::~iODT() {
}

void iODT::refreshIntrinsicDirectionalData() {
    intrinsicMesh.rebuildVertexSums();
    intrinsicMesh.computeCornerScaledAngles();
    intrinsicMesh.computeVertexAngleScales();
    intrinsicMesh.buildHalfedgeVectorsInVertex();
    intrinsicMesh.buildHalfedgeVectorsInFace();
}

bool iODT::optimalDelaunayTriangulation(int maxIterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    auto& conn = intrinsicMesh.getConnectivity();

    // Clear tracking 
    insertedVertices.clear();

    // Delaunay phase
    if (supportingHalfedge) {
        supportingHalfedge->makeDelaunay();
    } else {
        conn.makeDelaunay();
    }
    
    refreshIntrinsicDirectionalData();

    // Refinement phase
    if (!delaunayRefinement(maxIterations, minAngleDegrees)) {
        std::cerr << "[iODT] Delaunay refinement failed" << std::endl;
    }

    // Repositioning phase
    optimalReposition(maxIterations, 1e-4, maxEdgeLength, stepSize);

    return true;
}

int iODT::splitLongEdges(double maxEdgeLength, int maxSplits) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();
    int splitCount = 0;

    if (maxSplits <= 0) {
        maxSplits = std::numeric_limits<int>::max();
    }

    std::vector<std::pair<uint32_t, uint32_t>> edgesToSplit;
    edgesToSplit.reserve(edges.size());
    for (uint32_t e = 0; e < edges.size(); ++e) {
        if (edges[e].halfEdgeIdx == INVALID_INDEX) {
            continue;
        }
        if (!(edges[e].intrinsicLength > maxEdgeLength)) {
            continue;
        }
        edgesToSplit.push_back({ e, edges[e].halfEdgeIdx });
    }

    std::vector<uint32_t> newlyInsertedVertices;
    newlyInsertedVertices.reserve(edgesToSplit.size());

    for (const auto& [edgeIdx, heIdx] : edgesToSplit) {
        if (splitCount >= maxSplits) {
            break;
        }
        if (edgeIdx >= edges.size() || heIdx == INVALID_INDEX || heIdx >= halfEdges.size()) {
            continue;
        }

        uint32_t newVertex = INVALID_INDEX;
        uint32_t diagFront = INVALID_INDEX;
        uint32_t diagBack = INVALID_INDEX;
        if (splitEdge(edgeIdx, newVertex, diagFront, diagBack, heIdx, 0.5)) {
            splitCount++;
            newlyInsertedVertices.push_back(newVertex);
        }
    }

    if (splitCount > 0) {
        std::unordered_set<uint32_t> patchEdges;

        for (uint32_t newV : newlyInsertedVertices) {
            if (newV >= conn.getVertices().size()) {
                continue;
            }

            std::vector<uint32_t> localEdges = collectLocalDelaunayEdges(conn, newV);
            std::vector<uint32_t> flippedEdges;
            if (supportingHalfedge) {
                supportingHalfedge->makeDelaunayLocal(localEdges, &flippedEdges);
            } else {
                conn.makeDelaunayLocal(localEdges, &flippedEdges);
            }

            for (uint32_t edgeIdx : localEdges) {
                patchEdges.insert(edgeIdx);
            }
            for (uint32_t edgeIdx : flippedEdges) {
                patchEdges.insert(edgeIdx);
            }
        }

        updateLocalEdges(conn, std::vector<uint32_t>(patchEdges.begin(), patchEdges.end()));
    }

    return splitCount;
}

double iODT::repositionInsertedVertices(double stepSize) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();

    const double EPS_LEN = 1e-12;
    double maxMove = 0.0;

    if (insertedVertices.empty()) {
        return maxMove;
    }

    // Process each vertex
    for (uint32_t vIdx : insertedVertices) {
        if (vIdx >= verts.size())
            continue;
        if (conn.isBoundaryVertex(vIdx))
            continue;

        // Calculate weighted circumcenter displacement
        glm::dvec2 avgVec(0.0);
        double avgLen = 0.0;
        bool ok = computeWeightedCircumcenter(vIdx, avgVec, avgLen);

        if (!ok || avgLen <= EPS_LEN)
            continue;

        // Record movement for convergence tracking
        double moveLen = avgLen * stepSize;

        if (moveLen > maxMove)
            maxMove = moveLen;

        // Trace from vertex along displacement vector on intrinsic mesh
        if ((avgLen * stepSize) < 1e-12) {
            continue;
        }

        // Build 2D ring with vertex at origin
        auto ring = conn.buildVertexRing2D(vIdx);

        if (ring.neighborVertexIndices.empty()) {
            continue;
        }

        // New position in ring coords is the displacement from origin
        glm::dvec2 newPos2D = avgVec * stepSize;

        // Check for triangle inversion
        bool wouldInvert = false;

        for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
            size_t nextI = (i + 1) % ring.neighborVertexIndices.size();

            glm::dvec2 p_i = ring.neighborPositions2D[i];
            glm::dvec2 p_next = ring.neighborPositions2D[nextI];

            // Calculate signed area
            double det = (p_i.x - newPos2D.x) * (p_next.y - newPos2D.y) - (p_i.y - newPos2D.y) * (p_next.x - newPos2D.x);

            // If area is negative or zero, dont move
            if (det <= 1e-6) { 
                wouldInvert = true;
                break;
            }
        }

        if (wouldInvert) {
            continue; 
        }         

        auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
        
        // Call updateRemoval for all incident halfedges before changing lengths
        if (supportingHalfedge) {
            for (uint32_t he : outgoingHEs) {
                uint32_t mate = halfEdges[he].opposite;
                if (mate != INVALID_INDEX) {
                    supportingHalfedge->updateRemoval(mate);
                }
            }
        }
        
        // Update edge lengths
        for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
            uint32_t edgeIdx = ring.edgeIndices[i];
            glm::dvec2 neighborPos2D = ring.neighborPositions2D[i];

            double newLength = glm::length(neighborPos2D - newPos2D);
            edges[edgeIdx].intrinsicLength = newLength;
        }
        
        for (uint32_t fIdx : ring.faceIndices) {
            intrinsicMesh.updateCornerAnglesForFace(fIdx);
        }
        
        // Call updateInsertion for all incident halfedges after updating corner angles
        if (supportingHalfedge) {
            for (uint32_t he : outgoingHEs) {
                uint32_t mate = halfEdges[he].opposite;
                if (mate != INVALID_INDEX) {
                    supportingHalfedge->updateInsertion(mate);
                }
            }
        }

        updateLocalFaces(conn, ring.faceIndices);

        // Update the vertex's surface point correspondence on input mesh
        resolveVertex(vIdx);
    }

    return maxMove;
}

void iODT::optimalReposition(int maxIters, double tol, double maxEdgeLength, double stepSize)
{
    auto& conn = intrinsicMesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& halfEdges = conn.getHalfEdges();

    for (int iter = 0; iter < maxIters; ++iter) {
        int preSplitCount = splitLongEdges(maxEdgeLength, 0);
        double maxMove = repositionInsertedVertices(stepSize);
        int postSplitCount = splitLongEdges(maxEdgeLength, 0);

        if (insertedVertices.empty()) {
            return;
        }

        if (maxMove < tol && preSplitCount == 0 && postSplitCount == 0) {
            break;
        }
    }

    std::unordered_set<uint32_t> finalFaces;
    for (uint32_t vIdx : insertedVertices) {
        for (uint32_t faceIdx : conn.getVertexFaces(vIdx)) {
            if (faceIdx != HalfEdgeMesh::INVALID_INDEX) {
                finalFaces.insert(faceIdx);
            }
        }
    }
    if (!finalFaces.empty()) {
        updateLocalFaces(conn, std::vector<uint32_t>(finalFaces.begin(), finalFaces.end()));
    }
    
    // Update signpost angles for all inserted vertices
    for (uint32_t vIdx : insertedVertices) {
        if (vIdx >= verts.size()) {
            continue;
        }
        auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
        for (uint32_t heOut : outgoingHEs) {
            uint32_t heIn = halfEdges[heOut].opposite;
            if (heIn != HalfEdgeMesh::INVALID_INDEX) {
                intrinsicMesh.updateAngleFromCWNeighbor(heIn);
            }
        }
    }

    // Update 3D positions for inserted vertices based on their updated input locations
    for (uint32_t vIdx : insertedVertices) {
        auto it = intrinsicVertexLocations.find(vIdx);

        if (it == intrinsicVertexLocations.end())
            continue;

        const GeodesicTracer::SurfacePoint& sp = it->second;
        glm::dvec3 p3 = tracerInput.evaluateSurfacePoint(sp); 
        verts[vIdx].position = glm::vec3(p3);
    }
}

bool iODT::delaunayRefinement(int maxIters, float minAngleDegrees) {    
    const float MIN_AREA = 1e-5f;   // Stop refining small triangles
    const float MIN_ANGLE = minAngleDegrees * glm::pi<float>() / 180.0f;
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();

    std::priority_queue<FaceCandidate> faceQueue;
    std::deque<uint32_t> delaunayQueue;
    std::vector<uint8_t> inDelaunayQueue(conn.getEdges().size(), 0);
    std::vector<uint32_t> flippedEdges;

    const auto& faces = conn.getFaces();
    for (uint32_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
        queueRefineFace(faceIdx, MIN_ANGLE, MIN_AREA, faceQueue);
    }

    const auto& edges = conn.getEdges();
    for (uint32_t edgeIdx = 0; edgeIdx < edges.size(); ++edgeIdx) {
        queueDelaunayEdge(edgeIdx, delaunayQueue, inDelaunayQueue);
    }

    const int MAX_REFINEMENT_INSERTIONS = std::max(1, maxIters) * static_cast<int>(std::max<size_t>(faces.size(), 1));
    int insertions = 0;
    int recheckCount = 0;
    constexpr int MAX_RECHECK_COUNT = 5;

    while (insertions < MAX_REFINEMENT_INSERTIONS) {
        while (!delaunayQueue.empty()) {
            uint32_t edgeIdx = delaunayQueue.front();
            delaunayQueue.pop_front();
            if (edgeIdx < inDelaunayQueue.size()) {
                inDelaunayQueue[edgeIdx] = 0;
            }

            if (!isValidEdge(edgeIdx)) {
                continue;
            }

            const auto& edgesNow = conn.getEdges();
            uint32_t he = edgesNow[edgeIdx].halfEdgeIdx;
            if (he == INVALID_INDEX || he >= halfEdges.size()) {
                continue;
            }
            if (conn.isDelaunayEdge(he)) {
                continue;
            }

            bool flipped = supportingHalfedge ? supportingHalfedge->flipEdge(edgeIdx) : conn.flipEdge(edgeIdx);
            if (!flipped) {
                continue;
            }

            flippedEdges.push_back(edgeIdx);

            he = edgesNow[edgeIdx].halfEdgeIdx;
            if (he == INVALID_INDEX || he >= halfEdges.size()) {
                continue;
            }
            uint32_t heT = halfEdges[he].opposite;
            if (heT == INVALID_INDEX || heT >= halfEdges.size()) {
                continue;
            }

            uint32_t heN = halfEdges[he].next;
            uint32_t heTN = halfEdges[heT].next;
            uint32_t neighHalfedges[4] = {
                heN,
                (heN != INVALID_INDEX && heN < halfEdges.size()) ? halfEdges[heN].next : INVALID_INDEX,
                heTN,
                (heTN != INVALID_INDEX && heTN < halfEdges.size()) ? halfEdges[heTN].next : INVALID_INDEX
            };

            for (uint32_t nhe : neighHalfedges) {
                if (nhe == INVALID_INDEX || nhe >= halfEdges.size()) {
                    continue;
                }
                queueDelaunayEdge(conn.getEdgeFromHalfEdge(nhe), delaunayQueue, inDelaunayQueue);
            }

            uint32_t faceA = halfEdges[he].face;
            uint32_t faceB = halfEdges[heT].face;
            queueRefineFace(faceA, MIN_ANGLE, MIN_AREA, faceQueue);
            queueRefineFace(faceB, MIN_ANGLE, MIN_AREA, faceQueue);
        }

        if (!flippedEdges.empty()) {
            updateLocalEdges(conn, flippedEdges);
            flippedEdges.clear();
        }

        bool inserted = false;
        while (!faceQueue.empty()) {
            FaceCandidate candidate = faceQueue.top();
            faceQueue.pop();

            if (!isValidFace(candidate.faceIdx)) {
                continue;
            }

            float currentArea = intrinsicMesh.computeFaceArea(candidate.faceIdx);
            if (currentArea != candidate.area) {
                continue;
            }
            if (!needsRefinement(candidate.faceIdx, MIN_ANGLE, MIN_AREA)) {
                continue;
            }

            uint32_t newV = INVALID_INDEX;
            if (!insertCircumcenter(candidate.faceIdx, newV)) {
                continue;
            }
            if (newV == INVALID_INDEX || newV >= conn.getVertices().size()) {
                continue;
            }

            if (inDelaunayQueue.size() < conn.getEdges().size()) {
                inDelaunayQueue.resize(conn.getEdges().size(), 0);
            }

            for (uint32_t faceIdx : conn.getVertexFaces(newV)) {
                queueRefineFace(faceIdx, MIN_ANGLE, MIN_AREA, faceQueue);
                for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
                    queueDelaunayEdge(conn.getEdgeFromHalfEdge(he), delaunayQueue, inDelaunayQueue);
                }
            }

            ++insertions;
            inserted = true;
            break;
        }

        if (!inserted) {
            while (!delaunayQueue.empty()) {
                uint32_t edgeIdx = delaunayQueue.front();
                delaunayQueue.pop_front();
                if (edgeIdx < inDelaunayQueue.size()) {
                    inDelaunayQueue[edgeIdx] = 0;
                }
                if (!isValidEdge(edgeIdx)) {
                    continue;
                }
                const auto& edgesNow = conn.getEdges();
                uint32_t he = edgesNow[edgeIdx].halfEdgeIdx;
                if (he == INVALID_INDEX || he >= halfEdges.size()) {
                    continue;
                }
                if (conn.isDelaunayEdge(he)) {
                    continue;
                }
                bool flipped = supportingHalfedge ? supportingHalfedge->flipEdge(edgeIdx) : conn.flipEdge(edgeIdx);
                if (flipped) {
                    flippedEdges.push_back(edgeIdx);
                }
            }
            if (!flippedEdges.empty()) {
                updateLocalEdges(conn, flippedEdges);
                flippedEdges.clear();
            }

            bool anyFound = false;
            if (recheckCount < MAX_RECHECK_COUNT) {
                ++recheckCount;

                for (uint32_t faceIdx = 0; faceIdx < conn.getFaces().size(); ++faceIdx) {
                    const size_t before = faceQueue.size();
                    queueRefineFace(faceIdx, MIN_ANGLE, MIN_AREA, faceQueue);
                    anyFound = anyFound || faceQueue.size() != before;
                }

                for (uint32_t edgeIdx = 0; edgeIdx < conn.getEdges().size(); ++edgeIdx) {
                    if (!isValidEdge(edgeIdx)) {
                        continue;
                    }

                    uint32_t he = conn.getEdges()[edgeIdx].halfEdgeIdx;
                    if (he == INVALID_INDEX || he >= conn.getHalfEdges().size()) {
                        continue;
                    }

                    if (!conn.isDelaunayEdge(he)) {
                        queueDelaunayEdge(edgeIdx, delaunayQueue, inDelaunayQueue);
                        anyFound = true;
                    }
                }

                if (anyFound) {
                    continue;
                }
            }

            return true;
        }
    }

    std::cerr << "[iODT] Reached max refinement insertions" << std::endl;
    return true;
}

bool iODT::isValidFace(uint32_t faceIdx) const {
    const auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();
    if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    return conn.getFaceHalfEdges(faceIdx).size() == 3;
}

bool iODT::isValidEdge(uint32_t edgeIdx) const {
    const auto& conn = intrinsicMesh.getConnectivity();
    const auto& edges = conn.getEdges();
    const auto& halfEdges = conn.getHalfEdges();
    if (edgeIdx >= edges.size()) {
        return false;
    }

    uint32_t he = edges[edgeIdx].halfEdgeIdx;
    return he != HalfEdgeMesh::INVALID_INDEX && he < halfEdges.size();
}

bool iODT::needsRefinement(uint32_t faceIdx, float minAngleThreshold, float minAreaThreshold) {
    (void)minAreaThreshold;

    if (!isValidFace(faceIdx)) {
        return false;
    }

    const auto& conn = intrinsicMesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    std::vector<uint32_t> faceHEs = conn.getFaceHalfEdges(faceIdx);
    if (faceHEs.size() != 3) {
        return false;
    }

    if (minAngleThreshold <= 0.0f) {
        return false;
    }

    for (uint32_t he : faceHEs) {
        uint32_t v = halfEdges[he].origin;
        if (v != HalfEdgeMesh::INVALID_INDEX &&
            intrinsicMesh.getVertexAngleSum(v) < 2.0 * static_cast<double>(minAngleThreshold)) {
            return false;
        }
    }

    size_t needleCorners = countNeedleCornersGC(intrinsicMesh, faceIdx);

    if (needleCorners == 1) {
        return false;
    }

    bool hasRefinableSmallAngle = false;
    for (uint32_t he : faceHEs) {
        double angle = intrinsicMesh.getCornerAngle(he);
        if (!(angle > 0.0 && angle < minAngleThreshold)) {
            continue;
        }

        uint32_t heNext = halfEdges[he].next;
        if (heNext == HalfEdgeMesh::INVALID_INDEX || heNext >= halfEdges.size()) {
            continue;
        }

        uint32_t hePrev = halfEdges[heNext].next;
        bool isDegreeOneVertex = (hePrev != HalfEdgeMesh::INVALID_INDEX &&
                                  hePrev < halfEdges.size() &&
                                  halfEdges[he].opposite == hePrev);
        if (isDegreeOneVertex) {
            continue;
        }

        if (isFixedRefinementHalfedge(conn, he) &&
            isFixedRefinementHalfedge(conn, hePrev)) {
            continue;
        }

        hasRefinableSmallAngle = true;
        break;
    }

    return hasRefinableSmallAngle;
}

float iODT::refinementPriority(uint32_t faceIdx) {
    auto& conn = intrinsicMesh.getConnectivity();
    for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
        uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
        if (isFixedRefinementEdge(conn, edgeIdx)) {
            return std::numeric_limits<float>::infinity();
        }
    }

    return intrinsicMesh.computeFaceArea(faceIdx);
}

void iODT::queueRefineFace(uint32_t faceIdx, float minAngleThreshold, float minAreaThreshold, std::priority_queue<FaceCandidate>& faceQueue) {
    if (!needsRefinement(faceIdx, minAngleThreshold, minAreaThreshold)) {
        return;
    }

    FaceCandidate candidate;
    candidate.faceIdx = faceIdx;
    candidate.area = intrinsicMesh.computeFaceArea(faceIdx);
    candidate.priority = refinementPriority(faceIdx);
    faceQueue.push(candidate);
}

void iODT::queueDelaunayEdge(uint32_t edgeIdx, std::deque<uint32_t>& edgeQueue, std::vector<uint8_t>& inQueue) {
    if (!isValidEdge(edgeIdx)) {
        return;
    }
    if (edgeIdx >= inQueue.size()) {
        inQueue.resize(edgeIdx + 1, 0);
    }
    if (inQueue[edgeIdx]) {
        return;
    }
    edgeQueue.push_back(edgeIdx);
    inQueue[edgeIdx] = 1;
}

std::vector<uint32_t> iODT::collectLocalDelaunayEdges(HalfEdgeMesh& conn, uint32_t vertexIdx) {
    std::unordered_set<uint32_t> patchFaces;
    std::unordered_set<uint32_t> patchEdges;

    if (vertexIdx >= conn.getVertices().size()) {
        return {};
    }

    const auto& halfEdges = conn.getHalfEdges();
    for (uint32_t faceIdx : conn.getVertexFaces(vertexIdx)) {
        if (faceIdx == HalfEdgeMesh::INVALID_INDEX) {
            continue;
        }

        patchFaces.insert(faceIdx);
        for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
            if (he == HalfEdgeMesh::INVALID_INDEX || he >= halfEdges.size()) {
                continue;
            }

            uint32_t opp = halfEdges[he].opposite;
            if (opp != HalfEdgeMesh::INVALID_INDEX) {
                uint32_t oppFace = halfEdges[opp].face;
                if (oppFace != HalfEdgeMesh::INVALID_INDEX) {
                    patchFaces.insert(oppFace);
                }
            }
        }
    }

    for (uint32_t faceIdx : patchFaces) {
        for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
            uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
            if (edgeIdx != HalfEdgeMesh::INVALID_INDEX) {
                patchEdges.insert(edgeIdx);
            }
        }
    }

    return std::vector<uint32_t>(patchEdges.begin(), patchEdges.end());
}

void iODT::updateLocalFaces(HalfEdgeMesh& conn, const std::vector<uint32_t>& facePatch) {
    std::unordered_set<uint32_t> patchFaces;
    std::unordered_set<uint32_t> patchVertices;
    const auto& halfEdges = conn.getHalfEdges();

    for (uint32_t faceIdx : facePatch) {
        if (faceIdx == HalfEdgeMesh::INVALID_INDEX) {
            continue;
        }

        patchFaces.insert(faceIdx);
        for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
            if (he != HalfEdgeMesh::INVALID_INDEX && he < halfEdges.size()) {
                patchVertices.insert(halfEdges[he].origin);
            }
        }
    }

    std::vector<uint32_t> affectedVertices(patchVertices.begin(), patchVertices.end());
    std::vector<uint32_t> affectedFaces(patchFaces.begin(), patchFaces.end());

    intrinsicMesh.updateVertexSums(affectedVertices);
    intrinsicMesh.updateCornerScales(affectedVertices);
    intrinsicMesh.updateVertexScales(affectedVertices);
    intrinsicMesh.updateVertexVectors(affectedVertices);
    intrinsicMesh.updateFaceVectors(affectedFaces);
}

void iODT::updateLocalEdges(HalfEdgeMesh& conn, const std::vector<uint32_t>& edgePatch) {
    std::unordered_set<uint32_t> patchFaces;
    std::unordered_set<uint32_t> patchVertices;
    const auto& edges = conn.getEdges();
    const auto& halfEdges = conn.getHalfEdges();

    for (uint32_t edgeIdx : edgePatch) {
        if (edgeIdx >= edges.size()) {
            continue;
        }

        uint32_t he = edges[edgeIdx].halfEdgeIdx;
        if (he == HalfEdgeMesh::INVALID_INDEX || he >= halfEdges.size()) {
            continue;
        }

        uint32_t faceA = halfEdges[he].face;
        if (faceA != HalfEdgeMesh::INVALID_INDEX) {
            patchFaces.insert(faceA);
        }

        uint32_t opp = halfEdges[he].opposite;
        if (opp != HalfEdgeMesh::INVALID_INDEX && opp < halfEdges.size()) {
            uint32_t faceB = halfEdges[opp].face;
            if (faceB != HalfEdgeMesh::INVALID_INDEX) {
                patchFaces.insert(faceB);
            }
        }
    }

    for (uint32_t faceIdx : patchFaces) {
        for (uint32_t he : conn.getFaceHalfEdges(faceIdx)) {
            if (he != HalfEdgeMesh::INVALID_INDEX && he < halfEdges.size()) {
                patchVertices.insert(halfEdges[he].origin);
            }
        }
    }

    std::vector<uint32_t> affectedVertices(patchVertices.begin(), patchVertices.end());
    std::vector<uint32_t> affectedFaces(patchFaces.begin(), patchFaces.end());

    intrinsicMesh.updateVertexSums(affectedVertices);
    intrinsicMesh.updateCornerScales(affectedVertices);
    intrinsicMesh.updateVertexScales(affectedVertices);
    intrinsicMesh.updateVertexVectors(affectedVertices);
    intrinsicMesh.updateFaceVectors(affectedFaces);
}

bool iODT::insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex) {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();

    // Validate face index
    if (faceIdx >= faces.size()) {
        return false;
    }
    if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
        return false;
    }

    uint32_t he0 = faces[faceIdx].halfEdgeIdx;
    const auto& halfEdges = conn.getHalfEdges();
    if (he0 == INVALID_INDEX || he0 >= halfEdges.size()) {
        return false;
    }
    uint32_t he1 = halfEdges[he0].next;
    if (he1 == INVALID_INDEX || he1 >= halfEdges.size()) {
        return false;
    }
    uint32_t he2 = halfEdges[he1].next;
    if (he2 == INVALID_INDEX || he2 >= halfEdges.size() || halfEdges[he2].next != he0) {
        return false;
    }

    double a = conn.getIntrinsicLengthFromHalfEdge(he1);
    double b = conn.getIntrinsicLengthFromHalfEdge(he2);
    double c = conn.getIntrinsicLengthFromHalfEdge(he0);
    if (!(std::isfinite(a) && std::isfinite(b) && std::isfinite(c)) ||
        a <= 1e-12 || b <= 1e-12 || c <= 1e-12) {
        return false;
    }

    double a2 = a * a;
    double b2 = b * b;
    double c2 = c * c;
    glm::dvec3 circumcenterBary(
        a2 * (b2 + c2 - a2),
        b2 * (c2 + a2 - b2),
        c2 * (a2 + b2 - c2)
    );
    double barySum = circumcenterBary.x + circumcenterBary.y + circumcenterBary.z;
    if (std::abs(barySum) <= 1e-30) {
        return false;
    }
    circumcenterBary /= barySum;

    if (!std::isfinite(circumcenterBary.x) ||
        !std::isfinite(circumcenterBary.y) ||
        !std::isfinite(circumcenterBary.z)) {
        return false;
    }

    glm::dvec3 startBary(1.0 / 3.0);
    glm::dvec3 traceBaryVec = circumcenterBary - startBary;
    double traceNorm2 = glm::dot(traceBaryVec, traceBaryVec);

    if (traceNorm2 < 1e-24) {
        return false;
    }

    GeodesicTracer::GeodesicTraceResult intrinsicRes = tracer.traceFromFaceBarycentric(
        faceIdx,
        startBary,
        traceBaryVec
    );

    if (!intrinsicRes.success) {
        return false;
    }

    // Pick insertion type
    GeodesicTracer::SurfacePoint& surfacePoint = intrinsicRes.exitPoint;

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        uint32_t edgeIdx = surfacePoint.elementId;
        if (edgeIdx == INVALID_INDEX || edgeIdx >= conn.getEdges().size()) {
            return false;
        }

        uint32_t heSplit = conn.getEdges()[edgeIdx].halfEdgeIdx;
        uint32_t diagFront = INVALID_INDEX;
        uint32_t diagBack = INVALID_INDEX;
        if (!splitEdge(edgeIdx, outNewVertex, diagFront, diagBack, heSplit, 0.5)) {
            return false;
        }

        if (supportingHalfedge && intrinsicVertexLocations.count(outNewVertex)) {
            supportingHalfedge->trackInsertedVertex(outNewVertex, intrinsicVertexLocations[outNewVertex]);
        }

        return true;
    }

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        bool wasInserted = false;
        if (!insertPoint(surfacePoint.elementId, surfacePoint.baryCoords, outNewVertex, &wasInserted)) {
            return false;
        }
        if (!wasInserted) {
            return false;
        }
        return true;
    }

    return false;
}

bool iODT::insertPoint(uint32_t faceIdx, const glm::dvec3& baryCoords, uint32_t& outVertex, bool* outWasInserted) {
	if (outWasInserted) {
		*outWasInserted = false;
	}

	auto& conn = intrinsicMesh.getConnectivity();
	auto& halfEdges = conn.getHalfEdges();
	const auto& faces = conn.getFaces();

	if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
		return false;
	}

	const double EPS_BARY = 1e-8;

	glm::dvec3 b = baryCoords;
	if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) {
		return false;
	}

    b.x = std::max(0.0, b.x);
    b.y = std::max(0.0, b.y);
    b.z = std::max(0.0, b.z);
    double sum = b.x + b.y + b.z;
    if (sum <= 1e-16) {
        return false;
    }
    b /= sum;

    std::vector<uint32_t> faceHEs = conn.getFaceHalfEdges(faceIdx);
    if (faceHEs.size() != 3) {
        return false;
    }
    uint32_t he0 = faceHEs[0]; // v0 -> v1
    uint32_t he1 = faceHEs[1]; // v1 -> v2
    uint32_t he2 = faceHEs[2]; // v2 -> v0

    uint32_t v0 = halfEdges[he0].origin;
    uint32_t v1 = halfEdges[he1].origin;
    uint32_t v2 = halfEdges[he2].origin;

    // If the point is on an existing point then return it
    if (b.x >= 1.0 - EPS_BARY) {
        outVertex = v0;
        return true;
    }
    if (b.y >= 1.0 - EPS_BARY) {
        outVertex = v1;
        return true;
    }
    if (b.z >= 1.0 - EPS_BARY) {
        outVertex = v2;
        return true;
    }

    // If the point is on an edge then split it
    if (b.z <= EPS_BARY) {
        double denom = b.x + b.y;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.y / denom; // along v0->v1
        return splitEdge(he0, t, outVertex, outWasInserted);
    }

    if (b.x <= EPS_BARY) {
        double denom = b.y + b.z;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.z / denom; // along v1->v2
        return splitEdge(he1, t, outVertex, outWasInserted);
    }

    if (b.y <= EPS_BARY) {
        double denom = b.z + b.x;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.x / denom; // along v2->v0
        return splitEdge(he2, t, outVertex, outWasInserted);
    }

    // Else, insert into the face 
    auto tri2D = intrinsicMesh.layoutTriangle(faceIdx);
    glm::dvec2 P0 = tri2D.vertices[0];
    glm::dvec2 P1 = tri2D.vertices[1];
    glm::dvec2 P2 = tri2D.vertices[2];

    glm::dvec2 s2D = P0 * b.x + P1 * b.y + P2 * b.z;
    double R0 = glm::length(s2D - P0);
    double R1 = glm::length(s2D - P1);
    double R2 = glm::length(s2D - P2);

    // Call updateRemoval on boundary halfedges before topology changes
    if (supportingHalfedge) {
        for (uint32_t he : faceHEs) {
            supportingHalfedge->updateRemoval(he);
        }
    }

    uint32_t newV = conn.splitTriangleIntrinsic(faceIdx, R0, R1, R2);
    if (newV == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    // Internal halfedges from old vertices to newV are in the last 6 halfedges
    uint32_t baseIdx = static_cast<uint32_t>(halfEdges.size()) - 6;
    uint32_t he_to_v1 = baseIdx + 3; // v1 -> newV
    uint32_t he_to_v0 = baseIdx + 5; // v0 -> newV
    uint32_t he_to_v2 = baseIdx + 4; // v2 -> newV

    // Resize halfedge vectors buffer
    intrinsicMesh.getHalfedgeVectorsInVertex().resize(halfEdges.size(), glm::dvec2(0.0));

    // Update corner angles for only new faces around the inserted vertex
    auto newFaces = conn.getVertexFaces(newV);
    for (uint32_t fIdx : newFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            intrinsicMesh.updateCornerAnglesForFace(fIdx);
        }
    }

    // Update supporting halfedges after corner angles are set
    if (supportingHalfedge) {
        if (he_to_v0 < halfEdges.size()) {
            supportingHalfedge->updateInsertion(he_to_v0);
        }
        if (he_to_v1 < halfEdges.size()) {
            supportingHalfedge->updateInsertion(he_to_v1);
        }
        if (he_to_v2 < halfEdges.size()) {
            supportingHalfedge->updateInsertion(he_to_v2);
        }

        // Update the original boundary halfedges that were reused
        for (uint32_t he : faceHEs) {
            supportingHalfedge->updateInsertion(he);
        }
    }

    std::vector<uint32_t> affectedVertices;
    affectedVertices.reserve(faceHEs.size());
    for (uint32_t he : faceHEs) {
        if (he < halfEdges.size()) {
            affectedVertices.push_back(halfEdges[he].origin);
        }
    }

    std::vector<uint32_t> scaleVertices = affectedVertices;
    scaleVertices.push_back(newV);

    intrinsicMesh.updateVertexSums(scaleVertices);
    intrinsicMesh.updateCornerScales(scaleVertices);
    intrinsicMesh.updateVertexScales(scaleVertices);
    intrinsicMesh.updateVertexVectors(affectedVertices);
    intrinsicMesh.updateFaceVectors(newFaces);

    if (!resolveVertex(newV)) {
        return false;
    }

    insertedVertices.insert(newV);
    outVertex = newV;

    if (supportingHalfedge && intrinsicVertexLocations.count(newV)) {
        supportingHalfedge->trackInsertedVertex(newV, intrinsicVertexLocations[newV]);
    }
    if (outWasInserted) {
        *outWasInserted = true;
    }

    return true;
}

bool iODT::splitEdge(uint32_t heEdge, double tParam, uint32_t& outNewV, bool* outWasInserted) {
    if (outWasInserted) {
        *outWasInserted = false;
    }

    auto& conn = intrinsicMesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();

    const double EPS_T = 1e-8;

    tParam = std::clamp(tParam, 0.0, 1.0);
    if (tParam <= EPS_T) {
        outNewV = halfEdges[heEdge].origin;
        return true;
    }
    uint32_t heNext = halfEdges[heEdge].next;
    if (heNext == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }
    if (1.0 - tParam <= EPS_T) {
        outNewV = halfEdges[heNext].origin;
        return true;
    }

    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(heEdge);
    if (edgeIdx == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    uint32_t diagF = HalfEdgeMesh::INVALID_INDEX;
    uint32_t diagB = HalfEdgeMesh::INVALID_INDEX;
    uint32_t newV = HalfEdgeMesh::INVALID_INDEX;
    if (!splitEdge(edgeIdx, newV, diagF, diagB, heEdge, tParam)) {
        return false;
    }

    if (supportingHalfedge && intrinsicVertexLocations.count(newV)) {
        supportingHalfedge->trackInsertedVertex(newV, intrinsicVertexLocations[newV]);
    }
    if (outWasInserted) {
        *outWasInserted = true;
    }

    outNewV = newV;
    return true;
}

bool iODT::splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();

    if (edgeIdx >= edges.size()) {
        return false;
    }

    // Use the passed in halfedge 
    uint32_t parentHE = HESplit;
    uint32_t oppHE = halfEdges[parentHE].opposite;

    // Verify the halfedge actually belongs to this edge
    uint32_t edgeFromHE = conn.getEdgeFromHalfEdge(HESplit);
    if (edgeFromHE != edgeIdx) {
        return false;
    }

    uint32_t originalVA = halfEdges[parentHE].origin;
    uint32_t originalVB = halfEdges[halfEdges[parentHE].next].origin;

    // Set split fraction
    double splitFraction = t;

    // Get original edge length
    double originalLength = conn.getIntrinsicLengthFromHalfEdge(parentHE);

    if (originalLength <= 1e-12) {
        return false;
    }

    // Precalculate diagonal lengths before split
    std::vector<double> precomputedDiagonals;
    uint32_t face1 = halfEdges[parentHE].face;
    uint32_t face2 = (oppHE != HalfEdgeMesh::INVALID_INDEX)
        ? halfEdges[oppHE].face : HalfEdgeMesh::INVALID_INDEX;

    double diagLen1 = 0.0;
    if (face1 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen1 = intrinsicMesh.computeSplitDiagonalLength(face1, originalVA, originalVB, splitFraction);
    }

    double diagLen2 = 0.0;
    if (face2 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen2 = intrinsicMesh.computeSplitDiagonalLength(face2, originalVA, originalVB, splitFraction);
    }

    if (!(std::isfinite(diagLen1) && diagLen1 > 1e-12)) {
        return false;
    }

    if (face2 != HalfEdgeMesh::INVALID_INDEX && !(std::isfinite(diagLen2) && diagLen2 > 1e-12)) {
        return false;
    }

    precomputedDiagonals = { diagLen1, diagLen2 };

    // Call updateRemoval on the original edge halfedges before topology changes
    if (supportingHalfedge) {
        supportingHalfedge->updateRemoval(parentHE);
        if (oppHE != HalfEdgeMesh::INVALID_INDEX) {
            supportingHalfedge->updateRemoval(oppHE);
        }
    }

    // 5) Topology split
    edges[edgeIdx].halfEdgeIdx = parentHE;
    auto R = conn.splitEdgeTopo(edgeIdx, splitFraction);
    if (R.newV == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    uint32_t newV = R.newV;
    uint32_t diagFront = R.diagFront;
    uint32_t diagBack = R.diagBack;
    conn.getVertices()[newV].halfEdgeIdx = R.heB;

    // Set diagonal lengths before corner angle calculations
    for (int i = 0; i < 2; ++i) {
        uint32_t diagHE = (i == 0 ? R.diagFront : R.diagBack);
        double L = precomputedDiagonals[i];
        if (diagHE == HalfEdgeMesh::INVALID_INDEX) {
            continue;
        }

        uint32_t diagEdgeIdx = conn.getEdgeFromHalfEdge(diagHE);
        if (diagEdgeIdx != HalfEdgeMesh::INVALID_INDEX && diagEdgeIdx < edges.size()) {
            edges[diagEdgeIdx].intrinsicLength = L;
        }
    }

    // Resize buffers for the new vertex
    intrinsicMesh.getVertexAngleScales().resize(conn.getVertices().size(), 1.0);
    intrinsicMesh.getVertexAngleSums().resize(conn.getVertices().size(), 2.0 * glm::pi<double>());
    intrinsicMesh.getHalfedgeVectorsInVertex().resize(conn.getHalfEdges().size(), glm::dvec2(0.0));

    // Set the target angle sum for the new vertex
    bool isBoundary = (oppHE == HalfEdgeMesh::INVALID_INDEX ||
        halfEdges[oppHE].face == HalfEdgeMesh::INVALID_INDEX);
    intrinsicMesh.getVertexAngleSums()[newV] = isBoundary ? glm::pi<double>() : 2.0 * glm::pi<double>();

    // Update the corner angles only for affected faces
    auto adjacentFaces = conn.getVertexFaces(newV);
    for (uint32_t fIdx : adjacentFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            intrinsicMesh.updateCornerAnglesForFace(fIdx);
        }
    }

    // Update supporting halfedges
    if (supportingHalfedge) {
        // Update all halfedges incident to the new vertex
        auto newVertexHEs = conn.getVertexHalfEdges(newV);
        for (uint32_t he : newVertexHEs) {
            supportingHalfedge->updateInsertion(he);
            uint32_t opp = halfEdges[he].opposite;
            if (opp != HalfEdgeMesh::INVALID_INDEX) {
                supportingHalfedge->updateInsertion(opp);
            }
        }
    }

    if (!resolveVertex(newV)) {
        return false;
    }

    // Track inserted vertex and inserted surface point location
    insertedVertices.insert(newV);

    outNewVertex = newV;
    outDiagFront = diagFront;
    outDiagBack = diagBack;

    return true;
}

bool iODT::computeWeightedCircumcenter(uint32_t vertIdx, glm::dvec2& outAvgVec, double& outAvgLen) {
    const double EPS_LEN = 1e-12;

    auto& conn = intrinsicMesh.getConnectivity();
    outAvgVec = glm::dvec2(0.0);
    outAvgLen = 0.0;

    // Build 2D ring with vertex at origin 
    auto ring = conn.buildVertexRing2D(vertIdx);
    if (ring.neighborVertexIndices.empty()) {
        return false;
    }

    // Calculate area weighted average of vectors to circumcenters in ring coordinates
    glm::dvec2 accum(0.0);
    double accumW = 0.0;
    // Calculate circumcenter for each neighbor pair
    for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
        size_t nextI = (i + 1) % ring.neighborVertexIndices.size();

        // Find which face this corresponds to (face with halfedge from center to neighbor[i])
        uint32_t f = HalfEdgeMesh::INVALID_INDEX;
        if (i < ring.faceIndices.size()) {
            f = ring.faceIndices[i];
        }

        if (f == HalfEdgeMesh::INVALID_INDEX)
            continue;

        double area = intrinsicMesh.computeFaceArea(f);
        if (!(area > 0.0))
            continue;

        // Get the face's vertices in ring coordinates
        glm::dvec2 v0 = glm::dvec2(0.0, 0.0); 
        glm::dvec2 v1 = ring.neighborPositions2D[i];
        glm::dvec2 v2 = ring.neighborPositions2D[nextI];

        // Calculate circumcenter in ring coordinates
        glm::dvec2 cc2d = intrinsicMesh.computeCircumcenter2D(v0, v1, v2);
        if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y)) {
            continue;
        }

        // Vector from vertex (at origin) to circumcenter
        glm::dvec2 vectorToCircumcenter = cc2d;

        double L = glm::length(vectorToCircumcenter);
        if (L <= EPS_LEN)
            continue;

        // Area weighted accumulation
        accum += area * vectorToCircumcenter;
        accumW += area;
    }

    if (!(accumW > 0.0)) {
        return false;
    }

    outAvgVec = accum / accumW;
    outAvgLen = glm::length(outAvgVec);

    return true;
}

bool iODT::resolveVertex(uint32_t newVertexIdx) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& inputConn = inputMesh.getConnectivity();
    const auto& verts = conn.getVertices();
    const auto& halfEdges = conn.getHalfEdges();

    // Calculate angular coordinates for the halfedges
    auto outgoingHEs = conn.getVertexHalfEdges(newVertexIdx);
    std::vector<uint32_t> incomingHEs;
    for (uint32_t heOut : outgoingHEs) {
        uint32_t twinHe = halfEdges[heOut].opposite;
        if (twinHe != HalfEdgeMesh::INVALID_INDEX) {
            incomingHEs.push_back(twinHe);
        }
    }

    // Call updateAngleFromCWNeighbor on all incoming halfedges first 
    for (uint32_t heIn : incomingHEs) {
        intrinsicMesh.updateAngleFromCWNeighbor(heIn);
    }

    if (incomingHEs.empty()) {
        return false;
    }

    // Choose best adjacent vertex to trace from
    // Priority: original vertices (1) > inserted vertices (2) > boundary vertices (3)
    uint32_t inputTraceHe = incomingHEs[0]; // default
    int bestPriority = 9999;
    double bestLength = std::numeric_limits<double>::max();

    for (uint32_t heIn : incomingHEs) {
        uint32_t adjacentVertex = halfEdges[heIn].origin;

        // Skip vertices that dont have input location mapping
        if (intrinsicVertexLocations.find(adjacentVertex) == intrinsicVertexLocations.end()) {
            continue;
        }

        int priority = 2; // Default: inserted vertex

        // Check if its an original vertex 
        if (verts[adjacentVertex].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            priority = 1; // Original vertices have most stable locations
        }

        // Check if its a boundary edge
        uint32_t edgeIdx = halfEdges[heIn].edgeIdx;
        if (edgeIdx != HalfEdgeMesh::INVALID_INDEX) {
            const auto& edges = conn.getEdges();
            if (edgeIdx < edges.size()) {
                uint32_t oppositeHe = halfEdges[heIn].opposite;
                if (oppositeHe == HalfEdgeMesh::INVALID_INDEX ||
                    halfEdges[oppositeHe].face == HalfEdgeMesh::INVALID_INDEX) {
                    priority = 3;
                }
            }
        }

        // Calculate edge length for tie break
        double edgeLength = conn.getIntrinsicLengthFromHalfEdge(heIn);

        // Select best candidate 
        if (priority < bestPriority || (priority == bestPriority && edgeLength < bestLength)) {
            bestPriority = priority;
            bestLength = edgeLength;
            inputTraceHe = heIn;
        }
    }

    // Get the adjacent vertex to trace from 
    uint32_t traceFromVertex = halfEdges[inputTraceHe].origin;

    // Make sure trace vertex has input location mapping
    if (intrinsicVertexLocations.find(traceFromVertex) == intrinsicVertexLocations.end()) {
        // Initialize original vertex location
        if (traceFromVertex < verts.size() && verts[traceFromVertex].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::SurfacePoint location;
            location.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            location.elementId = verts[traceFromVertex].originalIndex;
            location.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
            location.split = 0.0;
            intrinsicVertexLocations[traceFromVertex] = location;
        }
        else {
            return false;
        }
    }

    // Trace from adjacent vertex to new vertex on input mesh 
    GeodesicTracer::SurfacePoint startPoint = intrinsicVertexLocations[traceFromVertex];

    // Get the outgoing halfedge from traceFromVertex that points to newVertexIdx
    uint32_t outgoingTraceHe = halfEdges[inputTraceHe].opposite;
    if (outgoingTraceHe == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    // Get intrinsic direction vector from the adjacent vertex's frame
    glm::dvec2 intrinsicTraceVec = intrinsicMesh.halfedgeVector(inputTraceHe);
    double traceLength = conn.getIntrinsicLengthFromHalfEdge(inputTraceHe);

    // Trace on input mesh to find where new vertex should be placed
    GeodesicTracer::GeodesicTraceResult inputTrace;
    if (startPoint.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        // Find reference face for vertex tracing
        auto outgoingHEs_input = inputConn.getVertexHalfEdges(startPoint.elementId);
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        for (uint32_t he : outgoingHEs_input) {
            uint32_t face = inputConn.getHalfEdges()[he].face;
            if (face != HalfEdgeMesh::INVALID_INDEX) {
                refFace = face;
                break;
            }
        }

        if (refFace != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::GeodesicTraceResult baseResult;
            baseResult.success = true;
            baseResult.distance = 0.0;
            inputTrace = tracerInput.traceFromVertex(startPoint.elementId, intrinsicTraceVec, traceLength, baseResult, traceLength);
        }
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // Get resolution face for this vertex
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        auto rfIt = vertexResolutionFaces.find(traceFromVertex);
        if (rfIt != vertexResolutionFaces.end()) {
            refFace = rfIt->second;
        }
        inputTrace = tracerInput.traceFromEdge(startPoint.elementId, startPoint.split, intrinsicTraceVec, traceLength, refFace);
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        inputTrace = tracerInput.traceFromFace(startPoint.elementId, startPoint.baryCoords, intrinsicTraceVec, traceLength);
    }

    if (!inputTrace.success) {
        return false;
    }

    // Set vertex location on input mesh using the actual exit point from trace
    GeodesicTracer::SurfacePoint newInputLocation = inputTrace.exitPoint;
    intrinsicVertexLocations[newVertexIdx] = newInputLocation;

    // Store which input face was used for tracing for tangent space alignment
    uint32_t resolutionFace = inputTrace.finalFaceIdx;

    if (resolutionFace != HalfEdgeMesh::INVALID_INDEX) {
        vertexResolutionFaces[newVertexIdx] = resolutionFace;
    }

    // Set the actual 3D position from the trace result
    auto& verticesMutable = const_cast<std::vector<HalfEdgeMesh::Vertex>&>(verts);
    verticesMutable[newVertexIdx].position = inputTrace.position3D;

    // Get the arrival direction from the input mesh trace
    glm::dvec2 outgoingVec(0.0);
    if (!inputTrace.steps.empty()) {
        const auto& finalStep = inputTrace.steps.back();
        // This is the outgoing vector 
        outgoingVec = -finalStep.dir2D;
    }
    else {
        outgoingVec = glm::dvec2(1.0, 0.0);
    }

    // Calculate incoming angle
    double incomingAngle = std::atan2(outgoingVec.y, outgoingVec.x);
    double standardizedAngle = intrinsicMesh.standardizeAngleForVertex(newVertexIdx, incomingAngle);

    // Set incoming = 0 for boundary halfedges
    if (!conn.isInteriorHalfEdge(inputTraceHe)) {
        standardizedAngle = 0.0;
    }

    // Set signpost angle for the traced halfedge opposite
    auto& halfEdgesMutable = const_cast<std::vector<HalfEdgeMesh::HalfEdge>&>(halfEdges);
    if (outgoingTraceHe != HalfEdgeMesh::INVALID_INDEX) {
        halfEdgesMutable[outgoingTraceHe].signpostAngle = standardizedAngle;
    }

    uint32_t firstHe = outgoingTraceHe;
    uint32_t currHe = HalfEdgeMesh::INVALID_INDEX;

    // Find CCW successor around the vertex
    if (firstHe != HalfEdgeMesh::INVALID_INDEX) {
        currHe = conn.getNextAroundVertex(firstHe);
    }

    // Orbit CCW updating angles
    while (currHe != HalfEdgeMesh::INVALID_INDEX && currHe != firstHe) {
        intrinsicMesh.updateAngleFromCWNeighbor(currHe);

        // Check if this is a boundary halfedge
        if (!conn.isInteriorHalfEdge(currHe)) {
            break;
        }

        // Move to next CCW halfedge around the same vertex
        currHe = conn.getNextAroundVertex(currHe);
    }
    return true;
}

double iODT::computeMinAngle(uint32_t faceIdx) {
    const auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();

    if (faceIdx >= faces.size()) {
        return 0.0f;
    }

    // Get the halfedges of this face
    std::vector<uint32_t> faceEdges = conn.getFaceHalfEdges(faceIdx);
    if (faceEdges.size() != 3) {
        return 0.0f;
    }

    // Get the edge lengths from edge objects
    double a = conn.getIntrinsicLengthFromHalfEdge(faceEdges[0]);
    double b = conn.getIntrinsicLengthFromHalfEdge(faceEdges[1]);
    double c = conn.getIntrinsicLengthFromHalfEdge(faceEdges[2]);

    // Minimum positive length for numerical stability
    a = std::max(a, 1e-5);
    b = std::max(b, 1e-5);
    c = std::max(c, 1e-5);

    // Calculate angles using the law of cosines
    double cosA = (b * b + c * c - a * a) / (2.0f * b * c);
    double cosB = (a * a + c * c - b * b) / (2.0f * a * c);
    double cosC = (a * a + b * b - c * c) / (2.0f * a * b);

    // Clamp to valid range to avoid numerical issues
    cosA = glm::clamp(cosA, -1.0, 1.0);
    cosB = glm::clamp(cosB, -1.0, 1.0);
    cosC = glm::clamp(cosC, -1.0, 1.0);

    // Convert to angles
    double angleA = std::acos(cosA);
    double angleB = std::acos(cosB);
    double angleC = std::acos(cosC);

    // Return the minimum angle
    return std::min(std::min(angleA, angleB), angleC);
}

bool iODT::isEdgeOriginal(uint32_t edgeIdx) const {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& edges = conn.getEdges();
    return edgeIdx < edges.size() ? edges[edgeIdx].isOriginal : false;
}

void iODT::initializeVertexLocations() {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& vertices = conn.getVertices();

    // Initialize all original vertices to map 1:1 to input model vertices
    for (uint32_t vIdx = 0; vIdx < vertices.size(); ++vIdx) {
        if (vertices[vIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) 
            continue;

        // Initialize all original vertices 
        if (vertices[vIdx].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::SurfacePoint location;
            location.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            location.elementId = vertices[vIdx].originalIndex;
            location.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
            location.split = 0.0;

            intrinsicVertexLocations[vIdx] = location;
        }
    }
}

GeodesicTracer::GeodesicTraceResult iODT::traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx) {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();
    const auto& inputVertices = inputMesh.getConnectivity().getVertices();

    if (intrinsicHalfedgeIdx >= HEs.size())
        return {};

    const auto& intrinsicHE = HEs[intrinsicHalfedgeIdx];
    uint32_t intrinsicStartV = intrinsicHE.origin;
    uint32_t intrinsicEndV = HEs[intrinsicHE.next].origin;
    uint32_t intrinsicEdgeIdx = conn.getEdgeFromHalfEdge(intrinsicHalfedgeIdx);

    // Short circuit for original edges
    if (isEdgeOriginal(intrinsicEdgeIdx)) {
        auto itA = intrinsicVertexLocations.find(intrinsicStartV);
        auto itB = intrinsicVertexLocations.find(intrinsicEndV);

        if (itA == intrinsicVertexLocations.end() || itB == intrinsicVertexLocations.end()) {
            return {};
        }

        GeodesicTracer::GeodesicTraceResult result;
        result.success = true;
        result.pathPoints = { itA->second, itB->second };
        result.distance = glm::length(intrinsicMesh.halfedgeVector(intrinsicHalfedgeIdx));
        
        // Add a single step
        GeodesicTracer::FaceStepResult step;
        step.success = true;
        step.distanceTraveled = result.distance;
        result.steps.push_back(step);
        
        return result;
    }

    // Lookup mapped endpoints
    auto itStart = intrinsicVertexLocations.find(intrinsicStartV);
    auto itEnd = intrinsicVertexLocations.find(intrinsicEndV);

    if (itStart == intrinsicVertexLocations.end()) {
        return {};
    }

    if (itEnd == intrinsicVertexLocations.end()) {
        return {};
    }

    // Check if the mapped vertices exist in input mesh
    GeodesicTracer::SurfacePoint startSP = itStart->second;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX &&
        startSP.elementId >= inputVertices.size()) {
        return {};
    }

    GeodesicTracer::SurfacePoint endSP = itEnd->second;

    // Get intrinsic vector
    glm::dvec2 traceVec = intrinsicMesh.halfedgeVector(intrinsicHalfedgeIdx);
    double traceLen = glm::length(traceVec);

    if (traceLen < 1e-12) {
        GeodesicTracer::GeodesicTraceResult result;
        result.success = true;
        result.pathPoints = { startSP, endSP };
        result.distance = 0.0;
        
        // Add a step with zero distance
        GeodesicTracer::FaceStepResult step;
        step.success = true;
        step.distanceTraveled = 0.0;
        result.steps.push_back(step);
        
        return result;
    }

    glm::dvec2 traceDir = traceVec / traceLen;

    GeodesicTracer::GeodesicTraceResult base;
    base.success = true;
    base.pathPoints.clear();
    base.pathPoints.push_back(startSP);
    base.distance = 0.0;
    
    uint32_t startVertex = startSP.elementId;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // For edge tracing, get the start vertex from the intrinsic halfedge
        auto& meshConn = intrinsicMesh.getConnectivity();
        const auto& halfEdge = meshConn.getHalfEdges()[intrinsicHalfedgeIdx];
        startVertex = halfEdge.origin;
    }

    // Trace on input mesh based on start point type
    GeodesicTracer::GeodesicTraceResult result;

    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        result = tracerInput.traceFromVertex(startSP.elementId, traceDir, traceLen, base, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        result = tracerInput.traceFromFace(startSP.elementId, startSP.baryCoords, traceDir, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {

        // Get the resolution face for the start vertex (which face's chart the direction is expressed in)
        uint32_t resolutionFace = HalfEdgeMesh::INVALID_INDEX;
        auto it = vertexResolutionFaces.find(startVertex);
        if (it != vertexResolutionFaces.end()) {
            resolutionFace = it->second;
        }

        result = tracerInput.traceFromEdge(startSP.elementId, startSP.split, traceDir, traceLen, resolutionFace);
    }

    if (result.success) {
        return result;
    }
    else {
        GeodesicTracer::GeodesicTraceResult fallback;
        fallback.success = false;
        fallback.pathPoints = { startSP, endSP };
        fallback.distance = traceLen;
        return fallback;
    }
}

std::vector<glm::vec3> iODT::getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const {
    std::vector<glm::vec3> polyline;

    // Trace the intrinsic halfedge to get surface points
    auto traceResult = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(intrinsicHalfedgeIdx);

    // Convert surface points to 3D positions
    for (const auto& sp : traceResult.pathPoints) {
        glm::dvec3 pos3D = tracerInput.evaluateSurfacePoint(sp);
        polyline.push_back(glm::vec3(pos3D));
    }

    return polyline;
}

void iODT::saveCommonSubdivisionOBJ(const std::string& filename, const Model& overlayModel) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[saveIntrinsicOverlayOBJ] Failed to open file: " << filename << std::endl;
        return;
    }

    // Extract vertices from the overlay model
    const auto& vertices = overlayModel.getVertices();
    const auto& indices = overlayModel.getIndices();

    // Write vertices
    for (const auto& vertex : vertices) {
        file << "v " << vertex.pos.x << " " << vertex.pos.y << " " << vertex.pos.z << "\n";
    }

    // Write vertex colors
    for (const auto& vertex : vertices) {
        file << "vc " << vertex.color.r << " " << vertex.color.g << " " << vertex.color.b << "\n";
    }

    // Write face elements 
    uint32_t faceCount = 0;
    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 < indices.size()) {
            // OBJ uses 1-based indexing
            file << "f " << (indices[i] + 1) << " " << (indices[i + 1] + 1) << " " << (indices[i + 2] + 1) << "\n";
            faceCount++;
        }
    }

    file.close();
}

void iODT::createCommonSubdivision(Model& overlayModel, std::vector<CommonSubdivision::IntrinsicTriangle>& outIntrinsicTriangles) {
    commonSubdivision = std::make_unique<CommonSubdivision>(
        intrinsicMesh,
        inputMesh,
        tracerInput,
        intrinsicVertexLocations,
        *this
    );
    
    commonSubdivision->build();  
    outIntrinsicTriangles = commonSubdivision->getIntrinsicTriangles();  
    commonSubdivision->exportToModel(overlayModel);
}

void iODT::cleanup() {
    supportingHalfedge.reset();
    commonSubdivision.reset();
}



