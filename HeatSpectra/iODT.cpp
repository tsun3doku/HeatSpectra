#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>

#include <iostream>
#include "glm/gtx/string_cast.hpp"
#include "glm/gtc/constants.hpp"
#include <chrono>
#include <algorithm>
#include <queue>

#include "iODT.hpp"

std::ostream& operator<<(std::ostream& os, const GeodesicTracer::SurfacePoint::Type& type) {
    switch (type) {
    case GeodesicTracer::SurfacePoint::Type::VERTEX: os << "VERTEX"; break;
    case GeodesicTracer::SurfacePoint::Type::EDGE:   os << "EDGE";   break;
    case GeodesicTracer::SurfacePoint::Type::FACE:   os << "FACE";   break;
    default:                                         os << "UNKNOWN"; break;
    }
    return os;
}   

iODT::iODT(Model& model, SignpostMesh& mesh) : model(model), mesh(mesh), tracer(mesh) {
    // Build the intrinsic mesh
    mesh.buildFromModel(model);
    auto& conn = mesh.getConnectivity();
    conn.rebuildConnectivity();
    conn.rebuildEdges();
    conn.rebuildOpposites();
    conn.initializeIntrinsicLengths();
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();
}

bool iODT::optimalDelaunayTriangulation(int maxIterations)
{
    auto& conn = mesh.getConnectivity();

    //  Clear tracking 
    insertedVertices.clear();
    recentlySplit.clear();

    // 1) Make mesh Delaunay

    std::cout << "\n Delaunay Flipping Phase " << std::endl;

    for (int iter = 0; iter < maxIterations; ++iter) {
#ifndef NDEBUG
        std::cout << "Delaunay pass " << (iter + 1) << "/" << maxIterations << "...\n";
#endif
        int flips = conn.makeDelaunay(1);
        if (flips == 0) {
            std::cout << "  no more flips needed.\n";
            break;
        }
    }

    conn.initializeIntrinsicLengths();
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();

    
    // 2) Delaunay refinement to improve triangle quality
    std::cout << "\n Delaunay Refinement Phase " << std::endl;
    if (!delaunayRefinement()) {
        std::cerr << "Warning: Delaunay refinement failed" << std::endl;
    }

  
    // 3) Global delaunay call
    std::cout << "\n Final Global Delaunay" << std::endl;

    for (int iter = 0; iter < maxIterations; ++iter) {
        std::cout << "Global Delaunay pass " << (iter + 1) << "/" << maxIterations << "...\n";
        int flips = conn.makeDelaunay(1);
        if (flips == 0) {
            std::cout << "  no more flips needed.\n";
            break;
        }
    }
    conn.rebuildEdges();
    conn.rebuildConnectivity();
    conn.initializeIntrinsicLengths();
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();  
    
    /*
    // 4) Optimal positioning of inserted vertices
    //std::cout << "\n Repositioning Phase" << std::endl;
    repositionInsertedVertices(4, 1e-4, 0.07); // Max edge length 
    // Update intrinsic data after repositioning
    conn.initializeIntrinsicLengths();
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();
    */

    // 5) Push the result back to the model and GPU
    mesh.applyToModel(model);
    model.recreateBuffers();

    return true;
}

void iODT::repositionInsertedVertices(int maxIters, double tol, double maxEdgeLength)
{
    auto& conn = mesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();

#ifndef NDEBUG
    std::cout << "Repositioning of " << insertedVertices.size() << " inserted vertices\n";
#endif
    if (insertedVertices.empty()) {
#ifndef NDEBUG
        std::cout << "  No inserted vertices to reposition\n";
#endif
        return;
    }

    const double EPS_LEN = 1e-12;
    const double DAMPING = 0.5;          // intrinsic step damping (0..1)
    const double START_EPS = 1e-8;       // small barycentric offset to avoid exact-vertex starts

    for (int iter = 0; iter < maxIters; ++iter) {
        // Edge splitting phase
#ifndef NDEBUG
        std::cout << "Iteration " << (iter + 1) << " - Edge splitting phase\n";
#endif
        int splitCount = 0;
        
        // Collect edges to split (do collection first to avoid invalidating iterators)
        std::vector<std::pair<uint32_t, uint32_t>> edgesToSplit; // pair of (edgeIdx, heIdx)
        for (uint32_t e = 0; e < edges.size(); ++e) {
            if (edges[e].halfEdgeIdx == INVALID_INDEX) continue;
            
            uint32_t he = edges[e].halfEdgeIdx;
            double length = halfEdges[he].intrinsicLength;
            
            if (length > maxEdgeLength) {
                edgesToSplit.push_back({e, he});
            }
        }
        
        // Split collected edges
        for (const auto& [edgeIdx, heIdx] : edgesToSplit) {
            uint32_t newVertex = INVALID_INDEX;
            uint32_t diagFront = INVALID_INDEX;
            uint32_t diagBack = INVALID_INDEX;
            
            if (splitEdge(edgeIdx, newVertex, diagFront, diagBack, heIdx, 0.5)) {
                insertedVertices.insert(newVertex);
                splitCount++;
            }
        }
        
#ifndef NDEBUG
        std::cout << "  Split " << splitCount << " edges\n";
#endif
        
        if (splitCount > 0) {
            // Update mesh data after splits
            mesh.updateAllCornerAngles({});
            mesh.updateAllSignposts();
            mesh.buildHalfedgeVectorsInVertex();
            mesh.buildHalfedgeVectorsInFace();
        }

        // Vertex repositioning phase
#ifndef NDEBUG
        std::cout << "Iteration " << (iter + 1) << " - Vertex repositioning phase\n";
#endif
        double maxMove = 0.0;
        int movedCount = 0;

        // track which vertices get an update this iteration 
        std::unordered_set<uint32_t> updatedThisIter;

        for (uint32_t vIdx : insertedVertices) {
            if (vIdx >= verts.size()) continue;
            if (conn.isBoundaryVertex(vIdx)) continue;

            // Calculate reference face, local corner, and area weighted average vector
            uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
            int localRefIdx = -1;
            glm::dvec2 avgVec(0.0);
            double avgLen = 0.0;
            bool ok = computeWeightedCircumcenter(vIdx, refFace, localRefIdx, avgVec, avgLen);
            if (!ok || avgLen <= EPS_LEN) continue;

            // compute direction in reference chart
            glm::dvec2 dir2d = avgVec / avgLen;

            // compute step: damped average length
            double stepLen = avgLen * DAMPING;

            // start bary slightly inside the reference triangle (avoid exact-vertex start)
            glm::dvec3 startBary(START_EPS, START_EPS, START_EPS);
            startBary[localRefIdx] = 1.0 - 2.0 * START_EPS;

            // --- intrinsic trace: follow averaged direction for stepLen ---
            GeodesicTracer::GeodesicTraceResult tr =
                tracer.traceFromFace(refFace, startBary, dir2d, stepLen);
            if (!tr.success) continue;

            // record movement metrics
            double moveLen = tr.distance;
            if (moveLen > tol) ++movedCount;
            if (moveLen > maxMove) maxMove = moveLen;

            // store intrinsic exit point for final 3D evaluation
            insertedLocation[vIdx] = tr.exitPoint;
            updatedThisIter.insert(vIdx);
        } // end loop over insertedVertices

        // Logging
#ifndef NDEBUG
        std::cout << "  Iteration " << (iter + 1)
            << ": moved " << movedCount
            << " vertices, max intrinsic step = " << maxMove << "\n";
#endif

        // Convergence test
        if (maxMove < tol) {
#ifndef NDEBUG
            std::cout << "  Converged after " << (iter + 1) << " iterations\n";
#endif
            break;
        }
    } 

    // Update metrics
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();

    // Update 3d positions for inserted vertices
    for (uint32_t vIdx : insertedVertices) {
        auto it = insertedLocation.find(vIdx);
        if (it == insertedLocation.end()) continue;
        const GeodesicTracer::SurfacePoint& sp = it->second;
        glm::dvec3 p3 = tracer.evaluateSurfacePoint(sp);
        verts[vIdx].position = glm::vec3(p3);
    }
#ifndef NDEBUG
    std::cout << "Repositioning complete\n";
#endif
}

bool iODT::delaunayRefinement() {
    const float MIN_ANGLE = 0.0f * glm::pi<float>() / 180.0f;
    const float MAX_AREA = 0.001f;
    const float MIN_AREA = 1e-4f;

    auto& conn = mesh.getConnectivity();

    std::unordered_set<uint32_t> processedFaces;

    for (int iter = 0; iter < 100; ++iter) {
#ifndef NDEBUG
        std::cout << "Refinement iteration " << (iter + 1) << "\n";
#endif

        mesh.updateAllCornerAngles({});
        auto cands = findRefinementCandidates(MIN_ANGLE, MAX_AREA);
        if (cands.empty()) {
#ifndef NDEBUG
            std::cout << "Done.\n";
#endif
            return true;
        }

        std::sort(cands.begin(), cands.end(),
            [&](auto const& a, auto const& b) { 
                return a.priority > b.priority; 
            });

        bool did = false;

        // Skip only for this iteration
        std::unordered_set<uint32_t> skipFaces; 

        // snapshot of global insertedVertices to detect new inserts added this iteration
        std::unordered_set<uint32_t> prevInserted = insertedVertices;
        // Set of inserts in this iteration
        std::unordered_set<uint32_t> iterNewVerts; 

        auto markNeighborsOf = [&](uint32_t v) {
            if (v >= conn.getVertices().size()) return;
            auto& H = conn.getHalfEdges();
            for (auto he : conn.getVertexHalfEdges(v)) {
                if (he == HalfEdgeMesh::INVALID_INDEX) 
                    continue;
                uint32_t fidx = H[he].face;
                if (fidx != HalfEdgeMesh::INVALID_INDEX) {
                    // skip for the rest of this iteration
                    skipFaces.insert(fidx); 
                }
            }
            };

        for (auto const& C : cands) {

            // Mid iteration skipping
            if (processedFaces.count(C.faceIdx)) 
                continue;
            if (skipFaces.count(C.faceIdx))      
                continue;

            // revalidate
            float  areaNow = mesh.computeFaceArea(C.faceIdx);
            double minAngNow = computeMinAngle(C.faceIdx);
            if (areaNow < MIN_AREA) 
                continue;

            bool angleBadNow = (MIN_ANGLE > 0.f) ? (minAngNow < MIN_ANGLE) : false;
            bool areaBadNow = (MAX_AREA > 0.f) ? (areaNow > MAX_AREA) : false;
            if (!(angleBadNow || areaBadNow)) {
                // Not a candidate
                continue; 
            }

            // Refine
            uint32_t newV = UINT32_MAX;
            bool success = insertCircumcenter(C.faceIdx, newV);
            if (!success) 
                continue;

            did = true;
            processedFaces.insert(C.faceIdx);

            // collect vertices actually created by this call
            std::vector<uint32_t> createdNow;
            if (newV != UINT32_MAX && newV < conn.getVertices().size()) {
                if (!iterNewVerts.count(newV)) {
                    createdNow.push_back(newV);
                    iterNewVerts.insert(newV);
                }
            }
            else {
                // detect new ones via global set diff
                for (uint32_t v : insertedVertices) {
                    if (!prevInserted.count(v) && !iterNewVerts.count(v)) {
                        createdNow.push_back(v);
                        iterNewVerts.insert(v);
                    }
                }
            }

            // skip neighbors of newly created verts for the rest of THIS iteration
            for (uint32_t v : createdNow) {
                markNeighborsOf(v);
            }

            // keep state consistent before next candidate
#ifndef NDEBUG
            std::cout << "Updating corner angles and signposts after flips...\n";
#endif
            mesh.updateAllCornerAngles({});
            mesh.updateAllSignposts();
            mesh.buildHalfedgeVectorsInVertex();
            mesh.buildHalfedgeVectorsInFace();
        }

        if (!did) {
            std::cout << "No refinement possible.\n";
            return true;
        }

        // next iteration: full rescan; skipFaces is cleared by scope
    }

    std::cout << "Reached max iterations\n";
    return true;
}

std::vector<iODT::RefinementCandidate>iODT::findRefinementCandidates(float minAngleThreshold, float maxAreaThreshold) {
    constexpr float MIN_AREA = 1e-4f;

    auto& conn = mesh.getConnectivity();
    auto const& F = conn.getFaces();
    std::vector<RefinementCandidate> out;
    out.reserve(F.size());

#ifndef NDEBUG
    std::cout << "  [DEBUG] minAngleThreshold (rad) = "
        << minAngleThreshold << " ("
        << (minAngleThreshold * 180.0f / glm::pi<float>())
        << " deg), maxAreaThreshold = " << maxAreaThreshold << "\n";
#endif

    int totalFaces = 0, skippedMinArea = 0, skippedZeroAngle = 0, skippedNotBad = 0, addedCandidates = 0;

    for (uint32_t f = 0; f < F.size(); ++f) {
        ++totalFaces;
        if (F[f].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) continue;

        float  area = mesh.computeFaceArea(f);
        double minAng = computeMinAngle(f);

        if (area < MIN_AREA) { ++skippedMinArea;  continue; }
        if (minAng <= 0.0) { ++skippedZeroAngle; continue; }

        bool angleBad = (minAngleThreshold > 0.f) ? (minAng < minAngleThreshold) : false;
        bool areaBad = (maxAreaThreshold > 0.f) ? (area > maxAreaThreshold) : false;

        if (!(angleBad || areaBad)) {
#ifndef NDEBUG
            std::cout << "[DEBUG] skip face " << f
                << " minAng=" << minAng << " thresh=" << minAngleThreshold
                << " area=" << area << " maxArea=" << maxAreaThreshold
                << " angleBad=" << angleBad << " areaBad=" << areaBad << "\n";
#endif
            
            ++skippedNotBad;
            continue;
        }

        RefinementCandidate rc{};
        rc.faceIdx = f;
        rc.minAngle = minAng;
        rc.area = area;
        rc.type = RefinementType::CIRCUMCENTER_INSERTION;
        rc.edgeIdx = HalfEdgeMesh::INVALID_INDEX;

        // safe priority
        float scoreArea = (maxAreaThreshold > 0.f) ? (area / maxAreaThreshold) : 0.f;
        float scoreAngle = (minAngleThreshold > 0.f) ? ((minAngleThreshold - float(minAng)) / minAngleThreshold) : 0.f;
        rc.priority = scoreArea + scoreAngle;

        out.push_back(rc);
        ++addedCandidates;
    }

#ifndef NDEBUG
    std::cout << "  [DEBUG] Face filtering: total=" << totalFaces
        << " skippedMinArea=" << skippedMinArea
        << " skippedZeroAngle=" << skippedZeroAngle
        << " skippedNotBad=" << skippedNotBad
        << " candidates=" << addedCandidates << std::endl;
#endif

    return out;
}

bool iODT::insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex) {
    auto& conn = mesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();
    const auto& faces = conn.getFaces();

#ifndef NDEBUG
    std::cout << "[insertCircumcenter] called on faceIdx = " << faceIdx << std::endl;
#endif

    // 1) Validate face index
    if (faceIdx >= faces.size()) {
#ifndef NDEBUG
        std::cout << "  -> faceIdx out of range. Returning false.\n";
#endif
        return false;
    }
    if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
#ifndef NDEBUG
        std::cout << "  -> face " << faceIdx << " already invalidated. Returning false.\n";
#endif
        return false;
    }

    // 2) Reject tiny faces
    float area = mesh.computeFaceArea(faceIdx);
#ifndef NDEBUG
    std::cout << "  -> face area = " << area << std::endl;
#endif
    if (area < 1e-8f) {
#ifndef NDEBUG
        std::cout << "  -> area < 1e-8, skipping insertion.\n";
#endif
        return false;
    }

    // 3) Layout original triangle in 2d
    auto triangle2D = mesh.layoutTriangle(faceIdx);
    glm::dvec2 P0 = triangle2D.vertices[0], P1 = triangle2D.vertices[1], P2 = triangle2D.vertices[2];

    // 4) Calculate 2d circumcenter 
    glm::dvec2 cc2d = mesh.computeCircumcenter2D(P0, P1, P2);
    if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y))
        return false;

#ifndef NDEBUG
    std::cout << "[insertCircumcenter] circumcenter 2D coords=(" << cc2d.x << "," << cc2d.y << ")" << std::endl;
#endif

    // 5) Pick corner furthest the circumcenter (smallest barycentric)
    glm::dvec3 circumcenterBary = mesh.computeBarycentric2D(cc2d, P0, P1, P2);
    int corner = 0;

#ifndef NDEBUG
    std::cout << "[insertCircumcenter] circumcenter bary coords=(" << circumcenterBary.x << "," << circumcenterBary.y << "," << circumcenterBary.z << ")" << std::endl;
#endif

    if (circumcenterBary.y < circumcenterBary.x)
        corner = 1;
    if (circumcenterBary.z < circumcenterBary[corner])
        corner = 2;
    
    constexpr double EPS = 1e-4;
    glm::dvec3 cornerB(EPS);
    cornerB[corner] = 1.0 - 2.0 * EPS;

    // Calculate the 2d start point inside the triangle
    glm::dvec2 start2D =
        P0 * cornerB.x +
        P1 * cornerB.y +
        P2 * cornerB.z;

    // 6) Build intrinsic vector ray
    glm::dvec2 dir2D = cc2d - start2D;
    double length = glm::length(dir2D);

    if (length < 1e-12)
        return false;

    dir2D /= length;

    // 7) Trace across face/faces
    GeodesicTracer::GeodesicTraceResult intrinsicRes = tracer.traceFromFace(
        faceIdx,     // start face
        cornerB,     // start corner bary
        dir2D,       // 2D direction in chart
        length       // circumradius
    );

    if (!intrinsicRes.success) {
#ifndef NDEBUG
        std::cout << "Intrinsic trace failed" << std::endl;
#endif
        return false;
    }

    // 8) Pick insertion type
    GeodesicTracer::SurfacePoint& surfacePoint = intrinsicRes.exitPoint;
 
#ifndef NDEBUG
    std::cout << "[insertCircumcenter] Intrinsic surface point of CC is type:" << surfacePoint.type
        << " with bary:(" << surfacePoint.baryCoords.x << "," << surfacePoint.baryCoords.y 
        << "," << surfacePoint.baryCoords.z << ")" << std::endl;
#endif

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
#ifndef NDEBUG
        std::cout << "[insertCircumcenter] trace hit existing vertex "
            << surfacePoint.elementId << ", skipping insertion\n";
#endif
         
        return false; 
    }

    // Record point location
    GeodesicTracer::SurfacePoint pendingInsert = surfacePoint;

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // EDGE split
        uint32_t edgeIdx = surfacePoint.elementId;
        double split = surfacePoint.split;
        
        // Pick a halfedge on the edge to pass to splitEdge
        uint32_t heSplit = conn.getEdges()[edgeIdx].halfEdgeIdx;
        if (heSplit == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
            std::cout << "[insertCircumcenter] invalid halfedge for edge " << edgeIdx << std::endl;
#endif
            return false;
        }

#ifndef NDEBUG
        std::cout << "[insertCircumcenter] splitting edge " << edgeIdx << " at t=" << split << " using halfedge " << heSplit << std::endl;
#endif
        
        splitEdge(edgeIdx, outNewVertex, outNewVertex, outNewVertex, heSplit, split);
        insertedLocation[outNewVertex] = pendingInsert;

        mesh.updateAllCornerAngles({});
        mesh.updateAllSignposts();
        mesh.buildHalfedgeVectorsInVertex();
        mesh.buildHalfedgeVectorsInFace();
        return true;

    } else {
        // FACE insert (circumcenter in start face)
        uint32_t targetFace = surfacePoint.elementId;
        glm::dvec3 targetBary = surfacePoint.baryCoords;

        // 9) Layout target face and calculate split radii
        auto triangleTarget = mesh.layoutTriangle(targetFace);
        glm::dvec2 V0 = triangleTarget.vertices[0], V1 = triangleTarget.vertices[1], V2 = triangleTarget.vertices[2];
        glm::dvec2 s2D = V0 * targetBary.x + V1 * targetBary.y + V2 * targetBary.z;
        double R0 = glm::length(s2D - V0);
        double R1 = glm::length(s2D - V1);
        double R2 = glm::length(s2D - V2);

        // 11) Trace target face to find the 3D position
        //    startHe = the halfedge of the original faceIdx at the chosen corner
        auto startHEs = conn.getFaceHalfEdges(faceIdx);
        uint32_t startHe = startHEs[corner];

#ifndef NDEBUG
        std::cout << "[insertCircumcenter] using startHe=" << startHe
            << " (origin vertex=" << halfEdges[startHe].origin << ")\n";
        std::cout << "[insertCircumcenter] calling resolveVertex..." << std::endl;
#endif
        glm::dvec3 resolvedPos;
        if (!tracer.resolveVertex(faceIdx, startHe, intrinsicRes, resolvedPos)) {
#ifndef NDEBUG
            std::cout << "[insertCircumcenter] resolveVertex failed\n";
#endif
            return false;
        }

        // 10) Intrinsic split
        uint32_t newV = conn.splitTriangleIntrinsic(targetFace, R0, R1, R2);
        insertedLocation[newV] = pendingInsert;
        if (newV == HalfEdgeMesh::INVALID_INDEX)
            return false;

        mesh.updateAllCornerAngles({});
        mesh.updateAllSignposts();
        mesh.buildHalfedgeVectorsInVertex();
        mesh.buildHalfedgeVectorsInFace();

        // 13) Mark only new edges as not original
        for (uint32_t he : conn.getVertexHalfEdges(newV)) {
            uint32_t e = conn.getEdgeIndexFromHalfEdge(he);
            if (e < conn.getEdges().size()) {
                conn.getEdges()[e].isOriginal = false;
            }
        }
        conn.getVertices()[newV].position = resolvedPos;
        insertedVertices.insert(newV);
        outNewVertex = newV;

#ifndef NDEBUG
        std::cout << "[insertCircumcenter]  final 3D pos of newv=" << outNewVertex
            << " is " << conn.getVertices()[outNewVertex].position.x << ","
            << conn.getVertices()[outNewVertex].position.y << ","
            << conn.getVertices()[outNewVertex].position.z << "\n";

        if (targetFace != HalfEdgeMesh::INVALID_INDEX) {
            std::cout << "[insertCircumcenter] Successfully inserted vertex on face="
                << targetFace << std::endl;
        }
        else {
            std::cout << "[insertCircumcenter] Failed to insert vertex newV=" << outNewVertex
                << " on face=" << targetFace << "\n";
        }
#endif

        return true;
    }
}

bool iODT::splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t) {
    auto& conn = mesh.getConnectivity();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();
    auto& faces = conn.getFaces();

    if (edgeIdx >= edges.size()) {
#ifndef NDEBUG
        std::cout << "[splitEdge] Invalid edge index\n";
#endif
        return false;
    }

    // 1) Use the provided halfedge directly
    uint32_t parentHE = HESplit;
    uint32_t oppHE = halfEdges[parentHE].opposite;

    // Verify the halfedge actually belongs to this edge
    uint32_t edgeFromHE = conn.getEdgeIndexFromHalfEdge(HESplit);
    if (edgeFromHE != edgeIdx) {
#ifndef NDEBUG
        std::cout << "[splitEdge] Provided halfedge " << HESplit
            << " doesn't belong to edge " << edgeIdx << ", aborting\n";
#endif
        return false;
    }

    uint32_t originalVA = halfEdges[parentHE].origin;
    uint32_t originalVB = halfEdges[halfEdges[parentHE].next].origin;

#ifndef NDEBUG
    std::cout << "[splitEdge] Splitting edge " << edgeIdx
        << " between vertices " << originalVA << " -> " << originalVB
        << " using halfedge " << HESplit << std::endl;
#endif

    // 2) Set split fraction
    double splitFraction = t;
#ifndef NDEBUG
    std::cout << "[splitEdge] Using provided split fraction: " << splitFraction << "\n";
#endif

    // 3) Get original halfedge length and compute child lengths
    double originalLength = halfEdges[parentHE].intrinsicLength;

    if (originalLength <= 1e-12) {
#ifndef NDEBUG
        std::cout << "[splitEdge] Halfedge had " << originalLength << " intrinsic length, skipping\n";
#endif
        return false;
    }

    double La = splitFraction * originalLength;
    double Lb = (1.0 - splitFraction) * originalLength;

#ifndef NDEBUG
    std::cout << "[splitEdge] originalLength=" << originalLength
        << " -> La=" << La << ", Lb=" << Lb << std::endl;
#endif

    // 4) Precompute diagonal lengths before split
    std::vector<double> precomputedDiagonals;
    uint32_t face1 = halfEdges[parentHE].face;
    uint32_t face2 = (oppHE != HalfEdgeMesh::INVALID_INDEX)
        ? halfEdges[oppHE].face : HalfEdgeMesh::INVALID_INDEX;

#ifndef NDEBUG
    std::cout << "[splitEdge] Computing diagonals for face1=" << face1
        << " and face2=" << face2 << std::endl;
#endif

    double diagLen1 = 0.0;
    if (face1 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen1 = mesh.computeSplitDiagonalLength(face1, originalVA, originalVB, splitFraction);
#ifndef NDEBUG
        std::cout << "[splitEdge] Face1 (" << face1 << ") diagonal length: " << diagLen1 << std::endl;
#endif
    }

    double diagLen2 = 0.0;
    if (face2 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen2 = mesh.computeSplitDiagonalLength(face2, originalVA, originalVB, splitFraction);
#ifndef NDEBUG
        std::cout << "[splitEdge] Face2 (" << face2 << ") diagonal length: " << diagLen2 << std::endl;
#endif
    }

    precomputedDiagonals = { diagLen1, diagLen2 };

    // 5) Topology split
    edges[edgeIdx].halfEdgeIdx = parentHE;
    auto R = conn.splitEdgeTopo(edgeIdx, splitFraction);
    if (R.newV == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
        std::cout << "[splitEdge] splitEdgeTopo failed\n";
#endif
        return false;
    }

    uint32_t newV = R.newV;
    uint32_t diagFront = R.diagFront;
    uint32_t diagBack = R.diagBack;
    conn.getVertices()[newV].halfEdgeIdx = R.heB;

    uint32_t heA = R.heA;         // first child of split edge
    uint32_t heB = R.heB;         // second child of split edge
    uint32_t dF = diagFront;      // new diagonal front
    uint32_t dB = diagBack;       // new diagonal back

#ifndef NDEBUG
    std::cout << "[DEBUG] splitEdgeTopo results: newV=" << newV
        << ", R.heA=" << R.heA << " (origin=" << halfEdges[R.heA].origin << ")"
        << ", R.heB=" << R.heB << " (origin=" << halfEdges[R.heB].origin << ")" << std::endl;
#endif

    // 7) Apply diagonal lengths
    for (int i = 0; i < 2; ++i) {
        uint32_t diagHE = (i == 0 ? R.diagFront : R.diagBack);
        double L = precomputedDiagonals[i];
        if (diagHE == HalfEdgeMesh::INVALID_INDEX) continue;

        halfEdges[diagHE].intrinsicLength = L;
        uint32_t opp = halfEdges[diagHE].opposite;
        if (opp != HalfEdgeMesh::INVALID_INDEX)
            halfEdges[opp].intrinsicLength = L;
    }

    // 8) Update geometry in correct order
#ifndef NDEBUG
    std::cout << "[splitEdge] Updating intrinsic geometry..." << std::endl;
#endif

    // First: Update corner angles for the affected faces
    std::unordered_set<uint32_t> affectedFaces;
    if (diagFront != HalfEdgeMesh::INVALID_INDEX) {
        uint32_t frontFace = halfEdges[diagFront].face;
        if (frontFace != HalfEdgeMesh::INVALID_INDEX) {
            affectedFaces.insert(frontFace);
        }
    }
    if (diagBack != HalfEdgeMesh::INVALID_INDEX) {
        uint32_t backFace = halfEdges[diagBack].face;
        if (backFace != HalfEdgeMesh::INVALID_INDEX) {
            affectedFaces.insert(backFace);
        }
    }

    // Update metric data
    mesh.updateAllCornerAngles({});
    mesh.updateAllSignposts();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();

    // 9) Vertex resolution using SurfacePoint
    auto& verts = conn.getVertices();

    // Set up SurfacePoint for the split location
    GeodesicTracer::SurfacePoint loc;
    loc.type = GeodesicTracer::SurfacePoint::Type::EDGE;
    loc.elementId = edgeIdx;
    loc.baryCoords = glm::dvec3(1.0 - splitFraction, splitFraction, 0.0);

#ifndef NDEBUG
    std::cout << "[splitEdge] SurfacePoint on edge "
        << edgeIdx << " at t=" << splitFraction << std::endl;
#endif

    // Find 3D position using linear interpolation
    tracer.resolveVertexLinear(newV, loc, heA, heB);
#ifndef NDEBUG
    std::cout << "[splitEdge] Successfully resolved vertex position using linear interpolation\n";
#endif

    // 10) Track inserted vertex and inserted surface point location
    insertedLocation[newV] = loc;
    insertedVertices.insert(newV);

    outNewVertex = newV;
    outDiagFront = diagFront;
    outDiagBack = diagBack;

#ifndef NDEBUG
    std::cout << "[splitEdge] SUCCESS: newV=" << newV
        << ", diagFront=" << diagFront << ", diagBack=" << diagBack << std::endl;
#endif
    return true;
}

bool iODT::computeWeightedCircumcenter(uint32_t vertIdx, uint32_t& outRefFace, int& outLocalRefIdx, glm::dvec2& outAvgVec, double& outAvgLen) {
    constexpr bool DEBUG = false;
    const double START_EPS = 1e-6;
    const double EPS_LEN = 1e-12;

    auto& conn = mesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();
    const auto& hvf = mesh.getHalfedgeVectorsInFace(); // used by rotateVectorAcrossEdge

    outRefFace = HalfEdgeMesh::INVALID_INDEX;
    outLocalRefIdx = -1;
    outAvgVec = glm::dvec2(0.0);
    outAvgLen = 0.0;

    // ring + incident faces
    std::vector<uint32_t> ringHEs = conn.getVertexHalfEdges(vertIdx);
    if (ringHEs.empty()) {
        if (DEBUG) std::cout << "[WCC] vert " << vertIdx << " no halfedges\n";
        return false;
    }

    std::vector<uint32_t> incidentFaces;
    incidentFaces.reserve(ringHEs.size());
    for (uint32_t he : ringHEs) {
        if (he == HalfEdgeMesh::INVALID_INDEX) continue;
        uint32_t f = HEs[he].face;
        if (f == HalfEdgeMesh::INVALID_INDEX) continue;
        if (std::find(incidentFaces.begin(), incidentFaces.end(), f) == incidentFaces.end())
            incidentFaces.push_back(f);
    }
    if (incidentFaces.empty()) {
        if (DEBUG) std::cout << "[WCC] vert " << vertIdx << " has no incident faces\n";
        return false;
    }

    // pick reference face & local corner
    for (uint32_t f : incidentFaces) {
        auto tri = mesh.layoutTriangle(f);
        for (int i = 0; i < 3; ++i)
            if ((uint32_t)tri.indices[i] == vertIdx) {
                outRefFace = f;
                outLocalRefIdx = i;
                break;
            }
        if (outRefFace != HalfEdgeMesh::INVALID_INDEX) break;
    }
    if (outRefFace == HalfEdgeMesh::INVALID_INDEX) {
        if (DEBUG) std::cout << "[WCC] vert " << vertIdx << " no reference face\n";
        return false;
    }

    // reference corner halfedge (origin == vertIdx)
    uint32_t heRefCorner = HalfEdgeMesh::INVALID_INDEX;
    for (uint32_t fhe : conn.getFaceHalfEdges(outRefFace)) {
        if (fhe != HalfEdgeMesh::INVALID_INDEX && HEs[fhe].origin == vertIdx) {
            heRefCorner = fhe;
            break;
        }
    }
    if (heRefCorner == HalfEdgeMesh::INVALID_INDEX) {
        if (DEBUG) std::cout << "[WCC] vert " << vertIdx << " no heRefCorner\n";
        return false;
    }

    // Precompute ref chart->signpost rotation
    auto triRef = mesh.layoutTriangle(outRefFace);
    glm::dvec2 eRefChart = hvf.size() == HEs.size()
        ? hvf[heRefCorner]
        : (triRef.vertices[(outLocalRefIdx + 1) % 3] - triRef.vertices[outLocalRefIdx]);

    double refChartAngle = std::atan2(eRefChart.y, eRefChart.x);
    double angleRefCorner = HEs[heRefCorner].signpostAngle;

    auto normPi = [](double a)->double {
        while (a <= -glm::pi<double>()) a += 2.0 * glm::pi<double>();
        while (a > glm::pi<double>()) a -= 2.0 * glm::pi<double>();
        return a;
        };
    angleRefCorner = normPi(angleRefCorner);
    refChartAngle = normPi(refChartAngle);
    const double rotateChartToRefSignpost = normPi(angleRefCorner - refChartAngle);

    if (DEBUG) {
        auto triR = mesh.layoutTriangle(outRefFace);
        
        // Print info about all incident faces
        std::cout << "[WCC] vert " << vertIdx << " incident faces (" << incidentFaces.size() << " total):\n";
        for (uint32_t f : incidentFaces) {
            double minAngle = computeMinAngle(f);
            double area = mesh.computeFaceArea(f);
            auto tri = mesh.layoutTriangle(f);
            std::cout << "  face " << f << ": min angle=" << (minAngle * 180.0 / glm::pi<double>()) 
                     << " deg, area=" << area 
                     << ", vertices=[" << tri.indices[0] << "," << tri.indices[1] << "," << tri.indices[2] << "]\n";
        }
    }

    auto rotate2 = [](const glm::dvec2& v, double ang)->glm::dvec2 {
        double c = std::cos(ang), s = std::sin(ang);
        return { c * v.x - s * v.y, s * v.x + c * v.y };
        };

    // transportAroundVertex helper 
    auto transportAroundVertex = [&](uint32_t startFace, uint32_t heStartCorner,
        uint32_t targetFace, const glm::dvec2& vecStart)
        -> std::pair<bool, glm::dvec2>
        {
            if (startFace == targetFace) return { true, vecStart };
            glm::dvec2 bestVec(0.0); bool got = false;
            struct Attempt { bool forward; };
            Attempt attempts[2] = { {true},{false} };
            int ringCap = std::max<int>(8, (int)incidentFaces.size() + 4);

            for (const auto& at : attempts) {
                uint32_t currFace = startFace;
                uint32_t heCorner = heStartCorner;
                glm::dvec2 v = vecStart;

                for (int steps = 0; steps < ringCap && currFace != targetFace; ++steps) {
                    if (at.forward) {
                        uint32_t oldHe = heCorner;
                        uint32_t newHe = HEs[oldHe].opposite;
                        if (newHe == HalfEdgeMesh::INVALID_INDEX) break;
                        uint32_t nextFace = HEs[newHe].face;
                        if (nextFace == HalfEdgeMesh::INVALID_INDEX) break;
                        v = tracer.rotateVectorAcrossEdge(mesh, currFace, oldHe, nextFace, newHe, v);
                        uint32_t nextCorner = HEs[newHe].next;
                        if (nextCorner == HalfEdgeMesh::INVALID_INDEX || HEs[nextCorner].origin != vertIdx) break;
                        currFace = nextFace; heCorner = nextCorner;
                    }
                    else {
                        uint32_t oldHe = HEs[heCorner].prev;
                        if (oldHe == HalfEdgeMesh::INVALID_INDEX) break;
                        uint32_t newHe = HEs[oldHe].opposite;
                        if (newHe == HalfEdgeMesh::INVALID_INDEX) break;
                        uint32_t nextFace = HEs[newHe].face;
                        if (nextFace == HalfEdgeMesh::INVALID_INDEX) break;
                        v = tracer.rotateVectorAcrossEdge(mesh, currFace, oldHe, nextFace, newHe, v);
                        uint32_t nextCorner = newHe;
                        if (HEs[nextCorner].origin != vertIdx) break;
                        currFace = nextFace; heCorner = nextCorner;
                    }
                }
                if (currFace == targetFace) { bestVec = v; got = true; break; }
            }
            return { got, bestVec };
        };

    glm::dvec2 accum(0.0);
    double accumW = 0.0;
    int successCount = 0;

    for (uint32_t f : incidentFaces) {
        double area = mesh.computeFaceArea(f);
        if (!(area > 0.0)) continue;

        auto tri = mesh.layoutTriangle(f);
        glm::dvec2 cc2d = mesh.computeCircumcenter2D(
            tri.vertices[0], tri.vertices[1], tri.vertices[2]);
        if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y)) continue;

        int localIdx = -1;
        for (int i = 0; i < 3; ++i)
            if ((uint32_t)tri.indices[i] == vertIdx) { localIdx = i; break; }
        if (localIdx < 0) continue;

        glm::dvec3 startBary(START_EPS, START_EPS, START_EPS);
        startBary[localIdx] = 1.0 - 2.0 * START_EPS;

        glm::dvec2 vCorner = tri.vertices[localIdx];
        glm::dvec2 dir = cc2d - vCorner;
        double L = glm::length(dir);
        if (L <= EPS_LEN) continue;
        glm::dvec2 dirN = dir / L;

        auto tr = tracer.traceFromFace(f, startBary, dirN, L);
        if (!tr.success || tr.finalFaceIdx == GeodesicTracer::INVALID_INDEX) continue;

        auto triExit = mesh.layoutTriangle(tr.finalFaceIdx);

        // displacement = weighted sum of (Vi - Vcorner) using baryCoords, no absolute chart
        int localExit = -1;
        // If we hit an edge, try to find the vertex in one of the adjacent faces
        if (tr.exitPoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
            uint32_t edgeIdx = tr.exitPoint.elementId;
            auto& edges = conn.getEdges();
            if (edgeIdx < edges.size()) {
                uint32_t he = edges[edgeIdx].halfEdgeIdx;
                if (he != HalfEdgeMesh::INVALID_INDEX) {
                    // Try both faces adjacent to the edge
                    uint32_t face1 = HEs[he].face;
                    uint32_t face2 = HalfEdgeMesh::INVALID_INDEX;

                    // Safely get the opposite face
                    uint32_t oppHe = HEs[he].opposite;
                    if (oppHe != HalfEdgeMesh::INVALID_INDEX) {
                        face2 = HEs[oppHe].face;
                    }

                    // Validate and get edge endpoints
                    uint32_t v0 = HEs[he].origin;
                    uint32_t nextHe = HEs[he].next;
                    if (nextHe == HalfEdgeMesh::INVALID_INDEX) continue;
                    uint32_t v1 = HEs[nextHe].origin;

                    // Validate and clamp split parameter
                    double t = tr.exitPoint.split;
                    if (!std::isfinite(t)) continue;
                    t = glm::clamp(t, 0.0, 1.0);

                    uint32_t chosenFace = HalfEdgeMesh::INVALID_INDEX;
                    glm::dvec3 useBary(0.0);
                    auto chosenLayout = triExit; // Use same type as triExit
                    int chosenLocalExit = -1;

                    // Check first face
                    if (face1 != HalfEdgeMesh::INVALID_INDEX) {
                        auto triFace1 = mesh.layoutTriangle(face1);
                        // Find local indices for edge endpoints in this face
                        int local_v0 = -1, local_v1 = -1;
                        for (int i = 0; i < 3; ++i) {
                            if ((uint32_t)triFace1.indices[i] == v0) local_v0 = i;
                            if ((uint32_t)triFace1.indices[i] == v1) local_v1 = i;
                            if ((uint32_t)triFace1.indices[i] == vertIdx) chosenLocalExit = i;
                        }
                        if (local_v0 >= 0 && local_v1 >= 0 && chosenLocalExit >= 0) {
                            // Compute proper barycentric coordinates using local indices
                            glm::dvec2 edgePos = triFace1.vertices[local_v0] * (1.0 - t) +
                                triFace1.vertices[local_v1] * t;
                            useBary = mesh.computeBarycentric2D(edgePos,
                                triFace1.vertices[0],
                                triFace1.vertices[1],
                                triFace1.vertices[2]);
                            chosenFace = face1;
                            chosenLayout = triFace1;
                            localExit = chosenLocalExit;
                        }
                    }

                    // If not found, check second face
                    if (localExit < 0 && face2 != HalfEdgeMesh::INVALID_INDEX) {
                        auto triFace2 = mesh.layoutTriangle(face2);
                        // Find local indices for edge endpoints in this face
                        int local_v0 = -1, local_v1 = -1;
                        for (int i = 0; i < 3; ++i) {
                            if ((uint32_t)triFace2.indices[i] == v0) local_v0 = i;
                            if ((uint32_t)triFace2.indices[i] == v1) local_v1 = i;
                            if ((uint32_t)triFace2.indices[i] == vertIdx) chosenLocalExit = i;
                        }
                        if (local_v0 >= 0 && local_v1 >= 0 && chosenLocalExit >= 0) {
                            // Compute proper barycentric coordinates using local indices
                            glm::dvec2 edgePos = triFace2.vertices[local_v0] * (1.0 - t) +
                                triFace2.vertices[local_v1] * t;
                            useBary = mesh.computeBarycentric2D(edgePos,
                                triFace2.vertices[0],
                                triFace2.vertices[1],
                                triFace2.vertices[2]);
                            chosenFace = face2;
                            chosenLayout = triFace2;
                            localExit = chosenLocalExit;
                        }
                    }

                    // Apply changes only if we found a valid face
                    if (chosenFace != HalfEdgeMesh::INVALID_INDEX) {
                        tr.finalFaceIdx = chosenFace;
                        tr.baryCoords = useBary;
                        triExit = chosenLayout;
                    }
                }
            }
        }
        else {
            // Normal case: check the exit face
            for (int i = 0; i < 3; ++i)
                if ((uint32_t)triExit.indices[i] == vertIdx) { localExit = i; break; }
        }
        if (localExit < 0) continue;        glm::dvec2 dispInExit(0.0);
        for (int i = 0; i < 3; ++i) {
            if (i == localExit) continue;
            dispInExit += tr.baryCoords[i] * (triExit.vertices[i] - triExit.vertices[localExit]);
        }
        if (!std::isfinite(dispInExit.x) || !std::isfinite(dispInExit.y)) continue;
        if (glm::length(dispInExit) <= EPS_LEN) continue;

        uint32_t heExitCorner = HalfEdgeMesh::INVALID_INDEX;
        for (uint32_t fhe : conn.getFaceHalfEdges(tr.finalFaceIdx)) {
            if (fhe != HalfEdgeMesh::INVALID_INDEX && HEs[fhe].origin == vertIdx) {
                heExitCorner = fhe; break;
            }
        }
        if (heExitCorner == HalfEdgeMesh::INVALID_INDEX) continue;

        auto [ok, dispInRefChart] = transportAroundVertex(
            tr.finalFaceIdx, heExitCorner, outRefFace, dispInExit);

        if (!ok) {
            double angleExitCorner = normPi(HEs[heExitCorner].signpostAngle);
            double delta = normPi(angleRefCorner - angleExitCorner);
            dispInRefChart = rotate2(dispInExit, delta);
            if (DEBUG) {
                std::cout << "  [WCC] face " << f
                    << " transport FAIL -> fallback corner delta=" << delta
                    << " heExitCorner=" << heExitCorner
                    << " finalFace=" << tr.finalFaceIdx << "\n";
            }
        }

        glm::dvec2 dispInRefSignpost = rotate2(dispInRefChart, rotateChartToRefSignpost);
        double dispLen = glm::length(dispInRefSignpost);
        if (dispLen <= EPS_LEN) continue;

            if (DEBUG) {
#ifndef NDEBUG
                std::cout << "  [WCC] face " << f
                    << " area=" << area
                    << " tr.finalFace=" << tr.finalFaceIdx
                    << " heExitCorner=" << heExitCorner
                    << " dispLen=" << dispLen
                    << " (transported & signpost-aligned)\n";
#endif
            }        accum += area * dispInRefSignpost;
        accumW += area;
        ++successCount;
    }

    if (!(accumW > 0.0) || successCount == 0) {
        if (DEBUG) std::cout << "[WCC] vert " << vertIdx
            << " no successful contributions\n";
        return false;
    }

    outAvgVec = accum / accumW;
    outAvgLen = glm::length(outAvgVec);

    if (DEBUG) {
        std::cout << "[WCC] vert " << vertIdx
            << " successCount=" << successCount
            << " outAvgVec=(" << outAvgVec.x << "," << outAvgVec.y << ")"
            << " outAvgLen=" << outAvgLen << "\n";
    }
    return true;
}

bool iODT::traceWeightedVector(uint32_t vertIdx, double stepLen, GeodesicTracer::GeodesicTraceResult& outTrace) {
    constexpr bool DEBUG = true;
    const double START_EPS = 1e-8;

    uint32_t refFace;
    int localRefIdx;
    glm::dvec2 avgVec;
    double avgLen;

    if (!computeWeightedCircumcenter(vertIdx, refFace, localRefIdx, avgVec, avgLen)) {
        outTrace.success = false;
        return false;
    }

    if (avgLen <= 0.0) {
        outTrace.success = false;
        return false;
    }

    glm::dvec2 dir = avgVec / avgLen;

    glm::dvec3 startBary(START_EPS, START_EPS, START_EPS);
    startBary[localRefIdx] = 1.0 - 2.0 * START_EPS;

    outTrace = tracer.traceFromFace(refFace, startBary, dir, stepLen);

    if (DEBUG) {
        if (outTrace.success && outTrace.finalFaceIdx != GeodesicTracer::INVALID_INDEX) {
            auto triExit = mesh.layoutTriangle(outTrace.finalFaceIdx);
            glm::dvec2 exitChart = triExit.vertices[0] * outTrace.baryCoords.x
                + triExit.vertices[1] * outTrace.baryCoords.y
                + triExit.vertices[2] * outTrace.baryCoords.z;
            glm::dvec2 exitInRef = (outTrace.finalFaceIdx == refFace)
                ? exitChart
                : tracer.chartLocal2D(mesh, outTrace.finalFaceIdx, refFace, exitChart);
#ifndef NDEBUG
            std::cout << "[traceWeightedVector] vert=" << vertIdx
                << " stepLen=" << stepLen
                << " exitInRef=(" << exitInRef.x << "," << exitInRef.y << ")\n";
#endif
        }
        else {
#ifndef NDEBUG
            std::cout << "[traceWeightedVector] trace failed for vert=" << vertIdx << "\n";
#endif
        }
    }

    return outTrace.success;
}

double iODT::computeMinAngle(uint32_t faceIdx) {
    const auto& conn = mesh.getConnectivity();
    const auto& faces = conn.getFaces();
    const auto& halfEdges = conn.getHalfEdges();

    if (faceIdx >= faces.size()) {
        return 0.0f;
    }

    // Get the halfedges of this face
    std::vector<uint32_t> faceEdges = conn.getFaceHalfEdges(faceIdx);
    if (faceEdges.size() != 3) {
        return 0.0f;
    }

    // Get the edge lengths directly from the halfedges
    double a = halfEdges[faceEdges[0]].intrinsicLength;
    double b = halfEdges[faceEdges[1]].intrinsicLength;
    double c = halfEdges[faceEdges[2]].intrinsicLength;

    // Ensure minimum positive length for numerical stability
    a = std::max(a, 1e-5);
    b = std::max(b, 1e-5);
    c = std::max(c, 1e-5);

    // Compute angles using the law of cosines
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