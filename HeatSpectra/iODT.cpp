#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>

#include <iostream>
#include <fstream>
#include <cmath>
#include <complex>
#include <map>
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

iODT::iODT(Model& model, SignpostMesh& mesh) : model(model), mesh(mesh), tracer(mesh), tracerInput(inputMesh) {
    // Build the extrinsic input mesh 
    inputMesh.buildFromModel(model);
    auto& inputConn = inputMesh.getConnectivity();
    inputMesh.updateAllCornerAngles({});
    inputMesh.computeCornerScaledAngles();
    inputMesh.updateAllSignposts();

    inputMesh.computeVertexAngleScales();
    inputMesh.buildHalfedgeVectorsInVertex();
    inputMesh.buildHalfedgeVectorsInFace();

    // Build the intrinsic mesh
    mesh.buildFromModel(model);
    auto& conn = mesh.getConnectivity();
    mesh.updateAllCornerAngles({});
    mesh.computeCornerScaledAngles();
    mesh.updateAllSignposts();

    mesh.computeVertexAngleScales();
    mesh.buildHalfedgeVectorsInVertex();
    mesh.buildHalfedgeVectorsInFace();

    // Initialize vertex locations for tracing   
    initializeVertexLocations();

    // All edges start as original
    const auto& edges = conn.getEdges();
    for (size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].halfEdgeIdx != INVALID_INDEX) {
            conn.getEdges()[i].isOriginal = true;
        }
    }
}

bool iODT::optimalDelaunayTriangulation(int maxIterations) {
    auto& conn = mesh.getConnectivity();

    // Clear tracking 
    insertedVertices.clear();
    recentlySplit.clear();

    // 1) Make mesh Delaunay
    std::cout << "\nDelaunay Flipping Phase " << std::endl;
    conn.makeDelaunay(maxIterations);
    std::cout << " Done.\n";

    mesh.computeCornerScaledAngles();
    mesh.computeVertexAngleScales();
    mesh.buildHalfedgeVectorsInFace();

    // 2) Delaunay refinement to improve triangle quality
    std::cout << "\nDelaunay Refinement Phase " << std::endl;
    if (!delaunayRefinement()) {
        //std::cerr << "Warning: Delaunay refinement failed" << std::endl;
    }

    // 4) Optimal positioning of inserted vertices
    std::cout << "\nRepositioning Phase" << std::endl;
    repositionInsertedVertices(5, 1e-4, 0.018);
    std::cout << " Done.\n";

    // 5) (OPTIONAL) Push the intrinsic result back to the model and GPU for debugging
    //mesh.applyToModel(model);
    //model.recreateBuffers();

    return true;
}

void iODT::repositionInsertedVertices(int maxIters, double tol, double maxEdgeLength)
{
    auto& conn = mesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();

    //std::cout << "[Reposition] Repositioning of " << insertedVertices.size() << " inserted vertices\n";
    if (insertedVertices.empty()) {
        //std::cout << "[Reposition] No inserted vertices to reposition\n";
        return;
    }

    const double EPS_LEN = 1e-12;        // Small length offset
    const double STEP_SIZE = 0.25;       // Intrinsic step size (0-1), higher = faster convergence *LOWER THIS TO PREVENT INTERSECTIONS*
    const double START_EPS = 1e-8;       // Small barycentric offset 

    for (int iter = 0; iter < maxIters; ++iter) {
        // Edge splitting phase
        //std::cout << "[Reposition] Iteration " << (iter + 1) << " - Edge splitting phase\n";
        int splitCount = 0;

        // Collect edges to split
        std::cout << " Splitting long edges..." << std::endl;
        std::vector<std::pair<uint32_t, uint32_t>> edgesToSplit;
        for (uint32_t e = 0; e < edges.size(); ++e) {
            if (edges[e].halfEdgeIdx == INVALID_INDEX)
                continue;

            uint32_t he = edges[e].halfEdgeIdx;
            double length = edges[e].intrinsicLength;

            if (length > maxEdgeLength) {
                edgesToSplit.push_back({ e, he });
            }
        }

        // Split collected edges and track newly inserted vertices
        std::vector<uint32_t> newlyInsertedVertices;
        for (const auto& [edgeIdx, heIdx] : edgesToSplit) {
            uint32_t newVertex = INVALID_INDEX;
            uint32_t diagFront = INVALID_INDEX;
            uint32_t diagBack = INVALID_INDEX;

            if (splitEdge(edgeIdx, newVertex, diagFront, diagBack, heIdx, 0.5)) {
                insertedVertices.insert(newVertex);
                newlyInsertedVertices.push_back(newVertex);
                splitCount++;
            }
        }

        //std::cout << "[Reposition] Split " << splitCount << " edges\n";

        if (splitCount > 0) {
            // Update local faces and vertices after splits
            for (uint32_t vIdx : newlyInsertedVertices) {
                if (vIdx >= verts.size()) continue;

                // Update corner angles for all incident faces
                auto faces = conn.getVertexFaces(vIdx);
                for (uint32_t fIdx : faces) {
                    if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
                        mesh.updateCornerAnglesForFace(fIdx);
                    }
                }

                // Update signpost angles for incoming halfedges
                auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
                for (uint32_t heOut : outgoingHEs) {
                    uint32_t twinHe = halfEdges[heOut].opposite;
                    if (twinHe != HalfEdgeMesh::INVALID_INDEX) {
                        mesh.updateAngleFromCWNeighbor(twinHe);
                    }
                }
            }

            mesh.buildHalfedgeVectorsInFace();

            // Perform local Delaunay operation on edges around newly inserted vertices
            for (uint32_t newV : newlyInsertedVertices) {
                if (newV >= conn.getVertices().size()) continue;

                std::vector<uint32_t> localEdges;
                for (uint32_t he : conn.getVertexHalfEdges(newV)) {
                    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
                    if (edgeIdx != HalfEdgeMesh::INVALID_INDEX && !conn.getEdges()[edgeIdx].isOriginal) {
                        localEdges.push_back(edgeIdx);
                    }
                }
                conn.makeDelaunay(5, &localEdges);
            }
        }

        // Vertex repositioning phase
        //std::cout << "[Reposition] Iteration " << (iter + 1) << " - Vertex repositioning phase\n";
        double maxMove = 0.0;
        int movedCount = 0;

        // Process each vertex
        int processedCount = 0;
        for (uint32_t vIdx : insertedVertices) {
            if (vIdx >= verts.size())
                continue;
            if (conn.isBoundaryVertex(vIdx))
                continue;

            // Calculate weighted circumcenter displacement
            uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
            int localRefIdx = -1;
            glm::dvec2 avgVec(0.0);
            double avgLen = 0.0;
            bool ok = computeWeightedCircumcenter(vIdx, refFace, localRefIdx, avgVec, avgLen);

            if (!ok || avgLen <= EPS_LEN)
                continue;

            // Damped average length
            double stepLen = avgLen * STEP_SIZE;

            // Record movement for convergence tracking
            double moveLen = stepLen;

            if (moveLen > tol)
                ++movedCount;
            if (moveLen > maxMove)
                maxMove = moveLen;

            // Trace from vertex along displacement vector on intrinsic mesh
            glm::dvec2 displacement2D = avgVec * STEP_SIZE;
            double stepLength = glm::length(displacement2D);

            if (stepLength < 1e-12) {
                //std::cout << "[Reposition] Step length too small for vertex " << vIdx << "\n";
                continue;
            }

            glm::dvec2 dir2D = glm::normalize(displacement2D);

            // Build 2D ring with vertex at origin
            auto ring = conn.buildVertexRing2D(vIdx);

            if (ring.neighborVertexIndices.empty()) {
                //std::cout << "[Reposition] No neighbors for vertex " << vIdx << "\n";
                continue;
            }

            // New position in ring coords is the displacement from origin
            glm::dvec2 newPos2D = avgVec * STEP_SIZE;

            //std::cout << "[Reposition] Ring has " << ring.neighborVertexIndices.size() << " neighbors\n";
            //std::cout << "[Reposition] New position in ring: (" << newPos2D.x << ", " << newPos2D.y << ")\n";

            // Update all edge lengths in the ring
            auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
            for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
                uint32_t neighborIdx = ring.neighborVertexIndices[i];
                uint32_t edgeIdx = ring.edgeIndices[i];
                glm::dvec2 neighborPos2D = ring.neighborPositions2D[i];

                double newLength = glm::length(neighborPos2D - newPos2D);
                edges[edgeIdx].intrinsicLength = newLength;
            }

            // Update geometry for affected faces only 
            for (uint32_t fIdx : ring.faceIndices) {
                mesh.updateCornerAnglesForFace(fIdx);
            }

            // Update the vertex's surface point correspondence on input mesh
            GeodesicTracer::SurfacePoint dummyLoc;
            dummyLoc.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            dummyLoc.elementId = vIdx;

            bool resolveSuccess = resolveVertex(vIdx, dummyLoc);
            if (!resolveSuccess) {
                //std::cout << "  [Reposition] Failed to resolve vertex " << vIdx << " to input mesh\n";
            }
        }
        // Convergence test
        if (maxMove < tol) {
            //std::cout << " [Reposition] Converged after " << (iter + 1) << " iterations\n";
            std::cout << " Done.\n";
            break;
        }
    }

    // Final geometry update 
    mesh.computeCornerScaledAngles();
    mesh.computeVertexAngleScales();
    mesh.buildHalfedgeVectorsInFace();
    mesh.buildHalfedgeVectorsInVertex();

    // Update signpost angles for all inserted vertices
    for (uint32_t vIdx : insertedVertices) {
        if (vIdx >= verts.size()) continue;
        auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
        for (uint32_t heOut : outgoingHEs) {
            uint32_t heIn = halfEdges[heOut].opposite;
            if (heIn != HalfEdgeMesh::INVALID_INDEX) {
                mesh.updateAngleFromCWNeighbor(heIn);
            }
        }
    }

    // Update 3D positions for inserted vertices based on their updated input locations
    for (uint32_t vIdx : insertedVertices) {
        auto it = intrinsicVertexLocations.find(vIdx);

        if (it == intrinsicVertexLocations.end())
            continue;

        const GeodesicTracer::SurfacePoint& sp = it->second;
        glm::dvec3 p3 = tracerInput.evaluateSurfacePoint(sp);  // Use tracerInput to evaluate on input mesh
        verts[vIdx].position = glm::vec3(p3);
    }
    //std::cout << "[Reposition] Repositioning complete\n";
}

bool iODT::delaunayRefinement() {
    const float MIN_ANGLE = 35.0f * glm::pi<float>() / 180.0f;      // Keep min angle under 40
    const float MAX_AREA = 100.0f;                                  // Outdated, better to rely on edge length control in repositionInsertedVertices
    const float MIN_AREA = 1e-4f;                                   // For degenerate models

    auto& conn = mesh.getConnectivity();

    std::unordered_set<uint32_t> processedFaces;

    for (int iter = 0; iter < 100; ++iter) {
        std::cout << " Refinement iteration " << (iter + 1) << "\n";

        mesh.updateAllCornerAngles({});
        auto cands = findRefinementCandidates(MIN_ANGLE, MAX_AREA);
        if (cands.empty()) {
            std::cout << " Done.\n";
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

            // Revalidate candidate
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

            // Collect vertices for current iteration
            std::vector<uint32_t> createdNow;
            if (newV != UINT32_MAX && newV < conn.getVertices().size()) {
                if (!iterNewVerts.count(newV)) {
                    createdNow.push_back(newV);
                    iterNewVerts.insert(newV);
                }
            }
            else {
                // Detect new vertices by global set difference
                for (uint32_t v : insertedVertices) {
                    if (!prevInserted.count(v) && !iterNewVerts.count(v)) {
                        createdNow.push_back(v);
                        iterNewVerts.insert(v);
                    }
                }
            }

            // Perform local Delaunay operation on edges around newly inserted vertex
            if (newV != UINT32_MAX && newV < conn.getVertices().size()) {
                std::vector<uint32_t> localEdges;
                for (uint32_t he : conn.getVertexHalfEdges(newV)) {
                    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
                    if (edgeIdx != HalfEdgeMesh::INVALID_INDEX && !conn.getEdges()[edgeIdx].isOriginal) {
                        localEdges.push_back(edgeIdx);
                    }
                }
                conn.makeDelaunay(5, &localEdges);
            }

            // skip neighbors of newly created verts for the rest of this iteration
            for (uint32_t v : createdNow) {
                markNeighborsOf(v);
            }
        }

        if (!did) {
            //std::cout << "No refinement possible.\n";
            return true;
        }
        // Update halfedge vectors at the end of iteration
        mesh.computeVertexAngleScales();
        mesh.buildHalfedgeVectorsInVertex();
        mesh.buildHalfedgeVectorsInFace();
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

    /*
    std::cout << "  [RefinementCandidates] minAngleThreshold (rad) = "
        << minAngleThreshold << " ("
        << (minAngleThreshold * 180.0f / glm::pi<float>())
        << " deg), maxAreaThreshold = " << maxAreaThreshold << "\n";
    */
    int totalFaces = 0, skippedMinArea = 0, skippedZeroAngle = 0, skippedNotBad = 0, addedCandidates = 0;

    for (uint32_t f = 0; f < F.size(); ++f) {
        ++totalFaces;

        if (F[f].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX)
            continue;

        float  area = mesh.computeFaceArea(f);
        double minAng = computeMinAngle(f);

        if (area < MIN_AREA) {
            ++skippedMinArea;
            continue;
        }
        if (minAng <= 0.0) {
            ++skippedZeroAngle;
            continue;
        }

        bool angleBad = (minAngleThreshold > 0.f) ? (minAng < minAngleThreshold) : false;
        bool areaBad = (maxAreaThreshold > 0.f) ? (area > maxAreaThreshold) : false;

        if (!(angleBad || areaBad)) {
            /*
            std::cout << "[RefinementCandidates] skip face " << f
                << " minAng=" << minAng << " thresh=" << minAngleThreshold
                << " area=" << area << " maxArea=" << maxAreaThreshold
                << " angleBad=" << angleBad << " areaBad=" << areaBad << "\n";
            */
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

    /*
    std::cout << "  [RefinementCandidates] Face filtering: total=" << totalFaces
        << " skippedMinArea=" << skippedMinArea
        << " skippedZeroAngle=" << skippedZeroAngle
        << " skippedNotBad=" << skippedNotBad
        << " candidates=" << addedCandidates << std::endl;
    */

    return out;
}

bool iODT::insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex) {
    auto& conn = mesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();
    const auto& faces = conn.getFaces();

    //std::cout << "[insertCircumcenter] called on faceIdx = " << faceIdx << std::endl;

    // 1) Validate face index
    if (faceIdx >= faces.size()) {
        //std::cout << "  -> faceIdx out of range. Returning false.\n";
        return false;
    }
    if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
        //std::cout << "  -> face " << faceIdx << " already invalidated. Returning false.\n";
        return false;
    }

    // 2) Reject tiny faces
    float area = mesh.computeFaceArea(faceIdx);
    //std::cout << "  -> face area = " << area << std::endl;

    if (area < 1e-8f) {
        //std::cout << "  -> area < 1e-8, skipping insertion.\n";
        return false;
    }

    // 3) Layout original triangle in 2d
    auto triangle2D = mesh.layoutTriangle(faceIdx);
    glm::dvec2 P0 = triangle2D.vertices[0], P1 = triangle2D.vertices[1], P2 = triangle2D.vertices[2];

    // 4) Calculate 2d circumcenter 
    glm::dvec2 cc2d = mesh.computeCircumcenter2D(P0, P1, P2);
    if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y))
        return false;

    //std::cout << "[insertCircumcenter] circumcenter 2D coords=(" << cc2d.x << "," << cc2d.y << ")" << std::endl;

    // 5) Pick corner furthest the circumcenter (smallest barycentric)
    glm::dvec3 circumcenterBary = mesh.computeBarycentric2D(cc2d, P0, P1, P2);
    int corner = 0;

    //std::cout << "[insertCircumcenter] circumcenter bary coords=(" << circumcenterBary.x << "," << circumcenterBary.y << "," << circumcenterBary.z << ")" << std::endl;

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
    auto faceHalfedges = conn.getFaceHalfEdges(faceIdx);
    uint32_t startVertex = HalfEdgeMesh::INVALID_INDEX;
    if (cornerB.z > 0.99)
        startVertex = faceHalfedges[2]; // mostly at corner 2
    else if (cornerB.y > 0.99)
        startVertex = faceHalfedges[1]; // mostly at corner 1  
    else if (cornerB.x > 0.99)
        startVertex = faceHalfedges[0]; // mostly at corner 0

    GeodesicTracer::GeodesicTraceResult intrinsicRes = tracer.traceFromFace(
        faceIdx,     // start face
        cornerB,     // start corner bary
        dir2D,       // 2D direction in chart
        length       // circumradius
    );

    if (!intrinsicRes.success) {
        //std::cout << "Intrinsic trace failed" << std::endl;
        return false;
    }

    // Pick insertion type
    GeodesicTracer::SurfacePoint& surfacePoint = intrinsicRes.exitPoint;

    /*
    std::cout << "[insertCircumcenter] Intrinsic surface point of CC is type:" << surfacePoint.type
        << " with bary:(" << surfacePoint.baryCoords.x << "," << surfacePoint.baryCoords.y
        << "," << surfacePoint.baryCoords.z << ")" << std::endl;
    */

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        //std::cout << "[insertCircumcenter] trace hit existing vertex " << surfacePoint.elementId << ", skipping insertion\n";

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
            //std::cout << "[insertCircumcenter] invalid halfedge for edge " << edgeIdx << std::endl;
            return false;
        }

        //std::cout << "[insertCircumcenter] splitting edge " << edgeIdx << " at t=" << split << " using halfedge " << heSplit << std::endl;

        auto& edges = conn.getEdges();
        splitEdge(edgeIdx, outNewVertex, outNewVertex, outNewVertex, heSplit, split);

        return true;
    }
    else {
        // FACE insert 
        uint32_t targetFace = surfacePoint.elementId;
        glm::dvec3 targetBary = surfacePoint.baryCoords;

        // Layout target face and calculate split radii
        auto triangleTarget = mesh.layoutTriangle(targetFace);
        glm::dvec2 V0 = triangleTarget.vertices[0], V1 = triangleTarget.vertices[1], V2 = triangleTarget.vertices[2];
        glm::dvec2 s2D = V0 * targetBary.x + V1 * targetBary.y + V2 * targetBary.z;
        double R0 = glm::length(s2D - V0);
        double R1 = glm::length(s2D - V1);
        double R2 = glm::length(s2D - V2);

        //  Get the halfedge of the original face at the chosen corner
        auto startHEs = conn.getFaceHalfEdges(faceIdx);
        uint32_t intrinsicStartHe = startHEs[corner];
        uint32_t intrinsicStartVertex = halfEdges[intrinsicStartHe].origin;

        //std::cout << "[insertCircumcenter] using intrinsic startHe=" << intrinsicStartHe << " (intrinsic vertex=" << intrinsicStartVertex << ")\n";
        //std::cout << "[insertCircumcenter] calling resolveVertex..." << std::endl;

        // Get the three original vertices of the face before splitting it
        auto oldHEs = conn.getFaceHalfEdges(targetFace);
        std::vector<uint32_t> oldVerts;
        for (uint32_t he : oldHEs) {
            oldVerts.push_back(conn.getHalfEdges()[he].origin);
        }

        // 11) Create the vertex topologically
        uint32_t newV = conn.splitTriangleIntrinsic(targetFace, R0, R1, R2);
        if (newV == HalfEdgeMesh::INVALID_INDEX)
            return false;

        // Resize buffers after topology change
        auto& halfEdges = conn.getHalfEdges();
        auto& vertices = conn.getVertices();

        // Resize halfedge vectors buffer
        mesh.getHalfedgeVectorsInVertex().resize(halfEdges.size(), glm::dvec2(0.0));

        // Resize vertex buffers
        mesh.getVertexAngleScales().resize(vertices.size(), 1.0);
        mesh.getVertexAngleSums().resize(vertices.size(), 2.0 * glm::pi<double>());

        // Get the three new faces surrounding the vertex
        auto newFaces = conn.getVertexFaces(newV);

        // Update the corner angles for only the new faces
        for (uint32_t fIdx : newFaces) {
            if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
                mesh.updateCornerAnglesForFace(fIdx);
            }
        }

        // Set the new vertex target angle sum before computing scales
        auto& vertexAngleSums = mesh.getVertexAngleSums();
        vertexAngleSums.resize(std::max<size_t>(vertexAngleSums.size(), newV + 1));
        vertexAngleSums[newV] = 2.0 * glm::pi<double>();

        // Now calculate vertex scales 
        mesh.computeVertexAngleScales();

        // Let resolveVertex handle signpost angle calculations
        if (!resolveVertex(newV, intrinsicRes.exitPoint)) {
            //std::cout << "[insertCircumcenter] resolveVertex failed\n";
            return false;
        }

        // Update halfedge vectors after vertex resolution (MAY NOT BE NEEDED)
        //mesh.buildHalfedgeVectorsInVertex();
        //mesh.buildHalfedgeVectorsInFace();

        insertedVertices.insert(newV);
        outNewVertex = newV;

        /*
        std::cout << "[insertCircumcenter]  final 3D pos of newv=" << outNewVertex
            << " is " << conn.getVertices()[outNewVertex].position.x << ","
            << conn.getVertices()[outNewVertex].position.y << ","
            << conn.getVertices()[outNewVertex].position.z << "\n";
        */

        if (targetFace != HalfEdgeMesh::INVALID_INDEX) {
            //std::cout << "[insertCircumcenter] Successfully inserted vertex on face=" << targetFace << std::endl;
        }
        else {
            //std::cout << "[insertCircumcenter] Failed to insert vertex newV=" << outNewVertex << " on face=" << targetFace << "\n";
        }

        return true;
    }
}

bool iODT::splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t) {
    auto& conn = mesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();
    auto& faces = conn.getFaces();

    if (edgeIdx >= edges.size()) {
        //std::cout << "[splitEdge] Invalid edge index\n";
        return false;
    }

    // Use the passed in halfedge 
    uint32_t parentHE = HESplit;
    uint32_t oppHE = halfEdges[parentHE].opposite;

    // Verify the halfedge actually belongs to this edge
    uint32_t edgeFromHE = conn.getEdgeFromHalfEdge(HESplit);
    if (edgeFromHE != edgeIdx) {
        //std::cout << "[splitEdge] Halfedge " << HESplit << " doesn't belong to edge " << edgeIdx << ", aborting\n";
        return false;
    }

    uint32_t originalVA = halfEdges[parentHE].origin;
    uint32_t originalVB = halfEdges[halfEdges[parentHE].next].origin;

    /*
    std::cout << "[splitEdge] Splitting edge " << edgeIdx
        << " between vertices " << originalVA << " -> " << originalVB
        << " using halfedge " << HESplit << std::endl;
    */
    // Set split fraction
    double splitFraction = t;

    //std::cout << "[splitEdge] Using split fraction: " << splitFraction << "\n";

    // Get original edge length and calculate child lengths
    double originalLength = conn.getIntrinsicLengthFromHalfEdge(parentHE);

    if (originalLength <= 1e-12) {
        //std::cout << "[splitEdge] Edge had " << originalLength << " intrinsic length, skipping\n";
        return false;
    }

    double La = splitFraction * originalLength;
    double Lb = (1.0 - splitFraction) * originalLength;

    //std::cout << "[splitEdge] originalLength=" << originalLength << " -> La=" << La << ", Lb=" << Lb << std::endl;

    // Precalculate diagonal lengths before split
    std::vector<double> precomputedDiagonals;
    uint32_t face1 = halfEdges[parentHE].face;
    uint32_t face2 = (oppHE != HalfEdgeMesh::INVALID_INDEX)
        ? halfEdges[oppHE].face : HalfEdgeMesh::INVALID_INDEX;

    //std::cout << "[splitEdge] Calculating diagonals for face1=" << face1 << " and face2=" << face2 << std::endl;

    double diagLen1 = 0.0;
    if (face1 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen1 = mesh.computeSplitDiagonalLength(face1, originalVA, originalVB, splitFraction);
        //std::cout << "[splitEdge] Face1 (" << face1 << ") diagonal length: " << diagLen1 << std::endl;
    }

    double diagLen2 = 0.0;
    if (face2 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen2 = mesh.computeSplitDiagonalLength(face2, originalVA, originalVB, splitFraction);
        //std::cout << "[splitEdge] Face2 (" << face2 << ") diagonal length: " << diagLen2 << std::endl;
    }

    precomputedDiagonals = { diagLen1, diagLen2 };

    // 5) Topology split
    edges[edgeIdx].halfEdgeIdx = parentHE;
    auto R = conn.splitEdgeTopo(edgeIdx, splitFraction);
    if (R.newV == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[splitEdge] splitEdgeTopo failed\n";
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
        if (diagHE == HalfEdgeMesh::INVALID_INDEX)
            continue;

        uint32_t diagEdgeIdx = conn.getEdgeFromHalfEdge(diagHE);
        if (diagEdgeIdx != HalfEdgeMesh::INVALID_INDEX && diagEdgeIdx < edges.size()) {
            edges[diagEdgeIdx].intrinsicLength = L;
            //std::cout << "[splitEdge] Set diagonal edge " << diagEdgeIdx << " length to " << L << std::endl;
        }
    }

    // Resize buffers for the new vertex
    mesh.getVertexAngleScales().resize(conn.getVertices().size(), 1.0);
    mesh.getVertexAngleSums().resize(conn.getVertices().size(), 2.0 * glm::pi<double>());
    mesh.getHalfedgeVectorsInVertex().resize(conn.getHalfEdges().size(), glm::dvec2(0.0));

    // Set the target angle sum for the new vertex
    bool isBoundary = (oppHE == HalfEdgeMesh::INVALID_INDEX ||
        halfEdges[oppHE].face == HalfEdgeMesh::INVALID_INDEX);
    mesh.getVertexAngleSums()[newV] = isBoundary ? glm::pi<double>() : 2.0 * glm::pi<double>();

    //std::cout << "[splitEdge] Set vertex " << newV << " angle sum to " << (isBoundary ? "pi" : "2pi") << " (boundary=" << isBoundary << ")" << std::endl;

    // Update the corner angles only for affected faces
    auto adjacentFaces = conn.getVertexFaces(newV);
    for (uint32_t fIdx : adjacentFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            mesh.updateCornerAnglesForFace(fIdx);
            //std::cout << "[splitEdge] Updated corner angles for face " << fIdx << std::endl;
        }
    }

    // Create surface point for the split location on the intrinsic mesh
    GeodesicTracer::SurfacePoint loc;
    loc.type = GeodesicTracer::SurfacePoint::Type::EDGE;
    loc.elementId = edgeIdx; // The intrinsic edge being split
    loc.split = splitFraction;
    loc.baryCoords = glm::dvec3(1.0 - splitFraction, splitFraction, 0.0);

    //std::cout << "[splitEdge] Created INTRINSIC EDGE SurfacePoint: edgeId=" << edgeIdx << " split=" << splitFraction << std::endl;

    // Let resolveVertex handle signpost angle calculations
    if (!resolveVertex(newV, loc)) {
        //std::cout << "[splitEdge] ERROR: resolveVertex failed for new vertex " << newV << std::endl;
        return false;
    }
    //std::cout << "[splitEdge] Successfully resolved vertex position using geodesic tracing\n";

    // Print final 3D position
    if (newV < verts.size()) {
        const auto& pos = verts[newV].position;
        //std::cout << "[splitEdge] final 3D pos of newv=" << newV << " is " << pos.x << "," << pos.y << "," << pos.z << std::endl;
    }

    // Track inserted vertex and inserted surface point location
    insertedVertices.insert(newV);

    outNewVertex = newV;
    outDiagFront = diagFront;
    outDiagBack = diagBack;

    //std::cout << "[splitEdge] SUCCESS: newV=" << newV << ", diagFront=" << diagFront << ", diagBack=" << diagBack << std::endl;

    return true;
}

bool iODT::computeWeightedCircumcenter(uint32_t vertIdx, uint32_t& outRefFace, int& outLocalRefIdx, glm::dvec2& outAvgVec, double& outAvgLen) {
    const double EPS_LEN = 1e-12;

    auto& conn = mesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();

    outRefFace = HalfEdgeMesh::INVALID_INDEX;
    outLocalRefIdx = -1;
    outAvgVec = glm::dvec2(0.0);
    outAvgLen = 0.0;

    // Build 2D ring with vertex at origin 
    auto ring = conn.buildVertexRing2D(vertIdx);
    if (ring.neighborVertexIndices.empty()) {
        return false;
    }

    // Pick first face as reference 
    if (!ring.faceIndices.empty()) {
        outRefFace = ring.faceIndices[0];
        outLocalRefIdx = 0; // Vertex is at origin in ring coords
    }

    // Calculate area weighted average of vectors to circumcenters in ring coordinates
    glm::dvec2 accum(0.0);
    double accumW = 0.0;
    int successCount = 0;

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

        double area = mesh.computeFaceArea(f);
        if (!(area > 0.0))
            continue;

        // Get the face's vertices in ring coordinates
        // Face is formed by center vertex and two consecutive neighbors
        glm::dvec2 v0 = glm::dvec2(0.0, 0.0); // Center vertex at origin
        glm::dvec2 v1 = ring.neighborPositions2D[i];
        glm::dvec2 v2 = ring.neighborPositions2D[nextI];

        // Calculate circumcenter in ring coordinates
        glm::dvec2 cc2d = mesh.computeCircumcenter2D(v0, v1, v2);
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
        ++successCount;
    }

    if (!(accumW > 0.0) || successCount == 0) {
        return false;
    }

    outAvgVec = accum / accumW;
    outAvgLen = glm::length(outAvgVec);

    return true;
}

bool iODT::resolveVertex(uint32_t newVertexIdx, const GeodesicTracer::SurfacePoint& intrinsicPoint) {
    auto& conn = mesh.getConnectivity();
    auto& inputConn = inputMesh.getConnectivity();
    const auto& verts = conn.getVertices();
    const auto& halfEdges = conn.getHalfEdges();

    //std::cout << "[resolveVertex] Resolving vertex " << newVertexIdx << " following Geometry Central pattern\n";

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
        double originalAngle = halfEdges[heIn].signpostAngle;
        mesh.updateAngleFromCWNeighbor(heIn);
        //std::cout << "[resolveVertex] Updated angle for incoming he=" << heIn << " from " << originalAngle << " to " << halfEdges[heIn].signpostAngle << "\n";
    }

    // Set up bases on the intrinsic faces
    auto adjacentFaces = conn.getVertexFaces(newVertexIdx);
    for (uint32_t fIdx : adjacentFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            // Update face coordinate system for this specific face
            mesh.updateCornerAnglesForFace(fIdx);
            //std::cout << "[resolveVertex] Updated face basis for face " << fIdx << "\n";
        }
    }

    if (incomingHEs.empty()) {
        //std::cout << "[resolveVertex] No incoming halfedges for vertex " << newVertexIdx << std::endl;
        return false;
    }

    // Choose best adjacent vertex to trace from
    // Priority: original vertices (1) > inserted vertices (2) > boundary vertices (3)
    uint32_t inputTraceHe = incomingHEs[0]; // default
    int bestPriority = 9999;
    double bestLength = std::numeric_limits<double>::max();

    for (uint32_t heIn : incomingHEs) {
        //std::cout << "[resolveVertex] Processing incoming halfedge " << heIn << "\n";

        uint32_t adjacentVertex = halfEdges[heIn].origin;

        // Skip vertices that don't have input location mapping
        if (intrinsicVertexLocations.find(adjacentVertex) == intrinsicVertexLocations.end()) {
            //std::cout << "[resolveVertex] Skipping adjacent vertex " << adjacentVertex << " - no input location mapping\n";
            continue;
        }
        //std::cout << "[resolveVertex] Considering adjacent vertex " << adjacentVertex << " with input location mapping\n";

        // Calculate priority (lower is better) 
        int priority = 2; // Default: inserted vertex

        // Check if it's an original vertex 
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
    //std::cout << "[resolveVertex] Selected trace halfedge " << inputTraceHe << " from vertex " << traceFromVertex << " (priority=" << bestPriority << ")\n";

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
            //std::cout << "[resolveVertex] Initialized input location for original vertex " << traceFromVertex << std::endl;
        }
        else {
            //std::cout << "[resolveVertex] ERROR: No input location for trace vertex " << traceFromVertex << std::endl;
            return false;
        }
    }

    // Trace from adjacent vertex to new vertex on input mesh 
    GeodesicTracer::SurfacePoint startPoint = intrinsicVertexLocations[traceFromVertex];

    // Get the outgoing halfedge from traceFromVertex that points to newVertexIdx
    uint32_t outgoingTraceHe = halfEdges[inputTraceHe].opposite;
    if (outgoingTraceHe == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[resolveVertex] ERROR: No outgoing halfedge for trace\n";
        return false;
    }

    // Get intrinsic direction vector from the adjacent vertex's frame
    glm::dvec2 intrinsicTraceVec = mesh.halfedgeVector(inputTraceHe);
    double traceLength = conn.getIntrinsicLengthFromHalfEdge(inputTraceHe);

    //std::cout << "[resolveVertex] Tracing on INPUT mesh from vertex " << traceFromVertex << " to vertex " << newVertexIdx << std::endl;
    //std::cout << "[resolveVertex] Intrinsic trace vector=(" << intrinsicTraceVec.x << "," << intrinsicTraceVec.y << ") length=" << traceLength << std::endl;

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
            inputTrace = tracerInput.traceFromVertex(startPoint.elementId, refFace, intrinsicTraceVec, traceLength, baseResult, traceLength);
        }
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // Get resolution face for this vertex
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        auto rfIt = vertexResolutionFaces.find(traceFromVertex);
        if (rfIt != vertexResolutionFaces.end()) {
            refFace = rfIt->second;
        }
        inputTrace = tracerInput.traceFromEdge(startPoint.elementId, startPoint.split, intrinsicTraceVec, traceLength, outgoingTraceHe, refFace);
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        inputTrace = tracerInput.traceFromFace(startPoint.elementId, startPoint.baryCoords, intrinsicTraceVec, traceLength);
    }

    if (!inputTrace.success) {
        //std::cout << "[resolveVertex] Input trace failed\n";
        return false;
    }

    // Set vertex location on input mesh using the actual exit point from trace
    GeodesicTracer::SurfacePoint newInputLocation = inputTrace.exitPoint;
    intrinsicVertexLocations[newVertexIdx] = newInputLocation;

    // Store which input face was used for tracing for tangent space alignment
    uint32_t resolutionFace = inputTrace.finalFaceIdx;
    if (resolutionFace == HalfEdgeMesh::INVALID_INDEX && !inputTrace.steps.empty()) {
        // Fallback: use the last face 
        resolutionFace = inputTrace.steps.back().finalBary.x >= 0 ? inputTrace.finalFaceIdx : HalfEdgeMesh::INVALID_INDEX;
    }
    if (resolutionFace != HalfEdgeMesh::INVALID_INDEX) {
        vertexResolutionFaces[newVertexIdx] = resolutionFace;
        //std::cout << "[resolveVertex] Stored resolution face=" << resolutionFace << " for vertex " << newVertexIdx << std::endl;
    }

    /*
    std::cout << "[resolveVertex] Set input location for vertex " << newVertexIdx
              << " to " << (newInputLocation.type == GeodesicTracer::SurfacePoint::Type::FACE ? "FACE" :
                           newInputLocation.type == GeodesicTracer::SurfacePoint::Type::EDGE ? "EDGE" : "VERTEX")
              << " " << newInputLocation.elementId << std::endl;
    */

    // Set the actual 3D position from the trace result
    auto& verticesMutable = const_cast<std::vector<HalfEdgeMesh::Vertex>&>(verts);
    verticesMutable[newVertexIdx].position = inputTrace.position3D;

    // Align the new vertex's tangent space to that of the input mesh
    // Get the arrival direction from the input mesh trace
    glm::dvec2 outgoingVec(0.0);
    if (!inputTrace.steps.empty()) {
        const auto& finalStep = inputTrace.steps.back();
        // This is the outgoing vector 
        outgoingVec = -finalStep.dir2D;
        //std::cout << "[resolveVertex] Using outgoing vector (" << outgoingVec.x << "," << outgoingVec.y << ")" << std::endl;
    }
    else {
        //std::cout << "[resolveVertex] WARNING: No trace steps available, using default vector (1,0)\n";
        outgoingVec = glm::dvec2(1.0, 0.0);
    }

    // Calculate incoming angle
    double incomingAngle = std::atan2(outgoingVec.y, outgoingVec.x);
    double standardizedAngle = mesh.standardizeAngleForVertex(newVertexIdx, incomingAngle);

    // Set incoming = 0 for boundary halfedges
    if (!conn.isInteriorHalfEdge(inputTraceHe)) {
        standardizedAngle = 0.0;
        //std::cout << "[resolveVertex] Traced halfedge is boundary, setting incomingAngle = 0.0\n";
    }

    // Set signpost angle for the traced halfedge opposite
    auto& halfEdgesMutable = const_cast<std::vector<HalfEdgeMesh::HalfEdge>&>(halfEdges);
    if (outgoingTraceHe != HalfEdgeMesh::INVALID_INDEX) {
        halfEdgesMutable[outgoingTraceHe].signpostAngle = standardizedAngle;
        //std::cout << "[resolveVertex] Set signpost angle " << standardizedAngle << " for halfedge " << outgoingTraceHe << std::endl;
    }

    // Custom loop to orbit CCW from inputTraceHe
    uint32_t firstHe = outgoingTraceHe;
    uint32_t currHe = HalfEdgeMesh::INVALID_INDEX;

    // Find currHe = firstHe.next().next().opp()
    if (firstHe != HalfEdgeMesh::INVALID_INDEX) {
        uint32_t nextHe = halfEdges[firstHe].next;
        if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
            nextHe = halfEdges[nextHe].next;
            if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
                currHe = halfEdges[nextHe].opposite;
            }
        }
    }

    // Orbit CCW updating angles
    while (currHe != HalfEdgeMesh::INVALID_INDEX && currHe != firstHe) {
        mesh.updateAngleFromCWNeighbor(currHe);
        //std::cout << "[resolveVertex] Updated signpost angle " << halfEdges[currHe].signpostAngle << " for halfedge " << currHe << std::endl;

        // Check if this is a boundary halfedge
        if (!conn.isInteriorHalfEdge(currHe)) {
            //std::cout << "[resolveVertex] Hit boundary halfedge " << currHe << ", stopping orbit\n";
            break;
        }

        // Move to next: currHe = currHe.next().next().opp()
        uint32_t nextHe = halfEdges[currHe].next;
        if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
            nextHe = halfEdges[nextHe].next;
            if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
                currHe = halfEdges[nextHe].opposite;
            }
            else {
                break;
            }
        }
        else {
            break;
        }
    }

    //std::cout << "[resolveVertex] Successfully resolved vertex " << newVertexIdx << std::endl;
    return true;
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
    auto& conn = mesh.getConnectivity();
    const auto& edges = conn.getEdges();
    return edgeIdx < edges.size() ? edges[edgeIdx].isOriginal : false;
}

void iODT::initializeVertexLocations() {
    auto& conn = mesh.getConnectivity();
    const auto& vertices = conn.getVertices();

    // Initialize all original vertices to map 1:1 to input model vertices
    for (uint32_t vIdx = 0; vIdx < vertices.size(); ++vIdx) {
        if (vertices[vIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) continue;

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

    //std::cout << "[initializeVertexLocations] Initialized " << intrinsicVertexLocations.size() << " vertex locations for original vertices\n";
}

void iODT::updateVertexLocation(uint32_t intrinsicVertexId, const GeodesicTracer::SurfacePoint& locationOnInput) {
    intrinsicVertexLocations[intrinsicVertexId] = locationOnInput;
}

std::vector<GeodesicTracer::SurfacePoint> iODT::traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx) {
    auto& conn = mesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();
    const auto& inputVertices = inputMesh.getConnectivity().getVertices();

    if (intrinsicHalfedgeIdx >= HEs.size())
        return {};

    const auto& intrinsicHE = HEs[intrinsicHalfedgeIdx];
    uint32_t intrinsicStartV = intrinsicHE.origin;
    uint32_t intrinsicEndV = HEs[intrinsicHE.next].origin;
    uint32_t intrinsicEdgeIdx = conn.getEdgeFromHalfEdge(intrinsicHalfedgeIdx);

    //std::cout << "[traceIntrinsicHalfedge] Tracing intrinsic halfedge " << intrinsicHalfedgeIdx << " (" << intrinsicStartV << "->" << intrinsicEndV << ") for edge " << intrinsicEdgeIdx << "\n";

    // Short circuit for original edges
    if (isEdgeOriginal(intrinsicEdgeIdx)) {
        auto itA = intrinsicVertexLocations.find(intrinsicStartV);
        auto itB = intrinsicVertexLocations.find(intrinsicEndV);
        if (itA == intrinsicVertexLocations.end() || itB == intrinsicVertexLocations.end()) {
            //std::cout << "[traceIntrinsicHalfedge] Missing vertex locations for original edge\n";
            return {};
        }
        return { itA->second, itB->second };
    }

    // Lookup mapped endpoints
    auto itStart = intrinsicVertexLocations.find(intrinsicStartV);
    auto itEnd = intrinsicVertexLocations.find(intrinsicEndV);

    if (itStart == intrinsicVertexLocations.end()) {
        //std::cout << "[traceIntrinsicHalfedge] Missing input mapping for start vertex " << intrinsicStartV << std::endl;
        return {};
    }
    if (itEnd == intrinsicVertexLocations.end()) {
        //std::cout << "[traceIntrinsicHalfedge] Missing input mapping for end vertex " << intrinsicEndV << std::endl;
        return {};
    }

    // Check if the mapped vertices exist in input mesh
    GeodesicTracer::SurfacePoint startSP = itStart->second;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX &&
        startSP.elementId >= inputVertices.size()) {
        //std::cout << "[traceIntrinsicHalfedge] Start vertex " << intrinsicStartV << " maps to invalid input vertex " << startSP.elementId << " (input mesh size=" << inputVertices.size() << ")" << std::endl;
        return {};
    }
    GeodesicTracer::SurfacePoint endSP = itEnd->second;

    // Get intrinsic vector
    glm::dvec2 traceVec = mesh.halfedgeVector(intrinsicHalfedgeIdx);
    double traceLen = glm::length(traceVec);

    if (traceLen < 1e-12)
        return { startSP, endSP };

    glm::dvec2 traceDir = traceVec / traceLen;

    GeodesicTracer::GeodesicTraceResult base;
    base.success = true;
    base.pathPoints.clear();
    base.pathPoints.push_back(startSP);
    base.distance = 0.0;

    // DEBUG: Print signpost angles for start vertex
    uint32_t startVertex = startSP.elementId;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // For edge tracing, get the actual start vertex from the intrinsic halfedge
        auto& meshConn = mesh.getConnectivity();
        const auto& halfEdge = meshConn.getHalfEdges()[intrinsicHalfedgeIdx];
        startVertex = halfEdge.origin;
    }

    auto& meshConn = mesh.getConnectivity();
    auto outgoingHalfedges = meshConn.getVertexHalfEdges(startVertex);
    //std::cout << "[DEBUG] Signpost angles for vertex " << startVertex << " (found " << outgoingHalfedges.size() << " halfedges):\n";
    for (uint32_t he : outgoingHalfedges) {
        if (he == HalfEdgeMesh::INVALID_INDEX) continue;
        const auto& heData = meshConn.getHalfEdges()[he];
        uint32_t endVertex = heData.next != HalfEdgeMesh::INVALID_INDEX ?
            meshConn.getHalfEdges()[heData.next].origin : HalfEdgeMesh::INVALID_INDEX;
        //std::cout << "  he" << he << " (" << startVertex << "->" << endVertex << "): " << heData.signpostAngle << std::endl;
    }

    // Trace on input mesh based on start point type
    GeodesicTracer::GeodesicTraceResult result;

    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        // Pick a reference face adjacent to the start vertex. Prioritize the stored resolution face.
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        auto it = vertexResolutionFaces.find(intrinsicStartV);
        if (it != vertexResolutionFaces.end()) {
            refFace = it->second;
            //std::cout << "[traceIntrinsicHalfedge] Using stored resolution face " << refFace << " for start vertex " << intrinsicStartV << std::endl;
        }

        // Fallback to finding any adjacent face if no resolution face is stored
        if (refFace == HalfEdgeMesh::INVALID_INDEX) {
            auto& inputConn = inputMesh.getConnectivity();
            auto outgoingHalfedges = inputConn.getVertexHalfEdges(startSP.elementId);
            for (uint32_t he : outgoingHalfedges) {
                if (he == HalfEdgeMesh::INVALID_INDEX)
                    continue;
                uint32_t f = inputConn.getHalfEdges()[he].face;
                if (f != HalfEdgeMesh::INVALID_INDEX) {
                    refFace = f;
                    break;
                }
            }
            //std::cout << "[traceIntrinsicHalfedge] Using fallback to find refFace " << refFace << " for start vertex " << intrinsicStartV << std::endl;
        }

        //std::cout << "[traceIntrinsicHalfedge] Tracing from VERTEX " << startSP.elementId << " dir=(" << traceDir.x << "," << traceDir.y << ") len=" << traceLen << "\n";

        result = tracerInput.traceFromVertex(startSP.elementId, refFace, traceDir, traceLen, base, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        //std::cout << "[traceIntrinsicHalfedge] Tracing from FACE " << startSP.elementId << " dir=(" << traceDir.x << "," << traceDir.y << ") len=" << traceLen << "\n";

        result = tracerInput.traceFromFace(startSP.elementId, startSP.baryCoords, traceDir, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        /*
        std::cout << "[traceIntrinsicHalfedge] Tracing from EDGE " << startSP.elementId
                  << " bary=(" << startSP.baryCoords.x << "," << startSP.baryCoords.y << ")"
                  << " dir=(" << traceDir.x << "," << traceDir.y << ") len=" << traceLen << "\n";
        */

        // Get the resolution face for the start vertex (which face's chart the direction is expressed in)
        uint32_t resolutionFace = HalfEdgeMesh::INVALID_INDEX;
        auto it = vertexResolutionFaces.find(startVertex);
        if (it != vertexResolutionFaces.end()) {
            resolutionFace = it->second;
            //std::cout << "[traceIntrinsicHalfedge] Found resolution face=" << resolutionFace << " for start vertex " << startVertex << std::endl;
        }
        else {
            //std::cout << "[traceIntrinsicHalfedge] WARNING: No resolution face stored for vertex " << startVertex << std::endl;
        }

        result = tracerInput.traceFromEdge(startSP.elementId, startSP.split, traceDir, traceLen, intrinsicHalfedgeIdx, resolutionFace);
    }

    if (result.success) {
        return result.pathPoints;
    }
    else {
        //std::cout << "[traceIntrinsicHalfedge] tracer failed, returning endpoints\n";
        return { startSP, endSP };
    }
}

std::vector<glm::vec3> iODT::mergeNearbyPoints(const std::vector<glm::vec3>& points, double tolerance) const {
    if (points.empty()) return points;

    std::vector<glm::vec3> merged;
    merged.reserve(points.size());

    for (const auto& point : points) {
        bool foundNearby = false;

        // Check if this point is close to any already added point
        for (auto& existing : merged) {
            if (glm::length(point - existing) <= tolerance) {
                // Merge by averaging positions 
                existing = (existing + point) * 0.5f;
                foundNearby = true;
                break;
            }
        }

        // If no nearby point found, add this as a new point
        if (!foundNearby) {
            merged.push_back(point);
        }
    }

    if (merged.size() != points.size()) {
        //std::cout << "[mergeNearbyPoints] Merged " << points.size() << " points -> " << merged.size() << " points (tolerance=" << tolerance << ")" << std::endl;
    }
    return merged;
}

void iODT::createCommonSubdivision(Model& overlayModel) const {
    auto& conn = mesh.getConnectivity();
    const auto& faces = conn.getFaces();
    const auto& halfEdges = conn.getHalfEdges();

    // Generate distinct colors for each intrinsic face using golden ratio spacing
    std::vector<glm::vec3> faceColors(faces.size());
    const float goldenRatioConjugate = 0.618033988749895f;
    float hue = 0.0f;

    for (size_t i = 0; i < faceColors.size(); ++i) {
        hue = std::fmod(hue + goldenRatioConjugate, 1.0f);
        // Vary saturation and value for wider color range
        float saturation = 0.55f + 0.2f * std::sin(static_cast<float>(i) * 0.8f);
        float value = 0.5f + 0.05f * std::cos(static_cast<float>(i) * 0.65f);

        // HSV to RGB conversion
        float c = value * saturation;
        float x = c * (1.0f - std::abs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
        float m = value - c;

        glm::vec3 rgb;
        if (hue < 1.0f / 6.0f) rgb = glm::vec3(c, x, 0);
        else if (hue < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0);
        else if (hue < 3.0f / 6.0f) rgb = glm::vec3(0, c, x);
        else if (hue < 4.0f / 6.0f) rgb = glm::vec3(0, x, c);
        else if (hue < 5.0f / 6.0f) rgb = glm::vec3(x, 0, c);
        else rgb = glm::vec3(c, 0, x);

        faceColors[i] = rgb + glm::vec3(m);
    }

    // Collect all polyline points from all faces and build face data
    std::vector<glm::vec3> allPoints;
    struct FacePolylines {
        uint32_t faceIdx;
        std::vector<std::vector<size_t>> edgePointIndices; // Indices into allPoints
    };
    std::vector<FacePolylines> faceData;

    for (uint32_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
        if (faces[faceIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) {
            continue;
        }

        // Get the 3 halfedges of this face
        uint32_t he0 = faces[faceIdx].halfEdgeIdx;
        uint32_t he1 = halfEdges[he0].next;
        uint32_t he2 = halfEdges[he1].next;

        if (he0 == HalfEdgeMesh::INVALID_INDEX ||
            he1 == HalfEdgeMesh::INVALID_INDEX ||
            he2 == HalfEdgeMesh::INVALID_INDEX) {
            continue;
        }

        // Get polylines for each edge
        std::vector<glm::vec3> poly0 = getCommonSubdivision(he0);
        std::vector<glm::vec3> poly1 = getCommonSubdivision(he1);
        std::vector<glm::vec3> poly2 = getCommonSubdivision(he2);

        if (poly0.empty() || poly1.empty() || poly2.empty()) {
            continue;
        }

        // Store face data and collect points
        FacePolylines fpData;
        fpData.faceIdx = faceIdx;
        fpData.edgePointIndices.resize(3);

        // Add poly0 points
        for (const auto& pt : poly0) {
            fpData.edgePointIndices[0].push_back(allPoints.size());
            allPoints.push_back(pt);
        }

        // Add poly1 points
        for (const auto& pt : poly1) {
            fpData.edgePointIndices[1].push_back(allPoints.size());
            allPoints.push_back(pt);
        }

        // Add poly2 points
        for (const auto& pt : poly2) {
            fpData.edgePointIndices[2].push_back(allPoints.size());
            allPoints.push_back(pt);
        }

        faceData.push_back(fpData);
    }

    // Merge nearby points globally
    //std::cout << "[createCommonSubdivision] Collected " << allPoints.size() << " points from " << faceData.size() << " faces\n";
    constexpr double MERGE_TOLERANCE = 1e-5;
    std::vector<glm::vec3> mergedPoints = mergeNearbyPoints(allPoints, MERGE_TOLERANCE);
    //std::cout << "[createCommonSubdivision] Merged to " << mergedPoints.size() << " unique points\n";

    // Build mapping from original points to merged points
    std::vector<uint32_t> pointMapping(allPoints.size());
    for (size_t i = 0; i < allPoints.size(); ++i) {
        for (size_t j = 0; j < mergedPoints.size(); ++j) {
            if (glm::length(allPoints[i] - mergedPoints[j]) <= MERGE_TOLERANCE) {
                pointMapping[i] = static_cast<uint32_t>(j);
                break;
            }
        }
    }

    // Build triangles using merged vertices
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Group points by input face, triangulate within each
    const uint32_t INVALID_INDEX = HalfEdgeMesh::INVALID_INDEX;

    // Store surface points for each original point during collection phase
    std::vector<GeodesicTracer::SurfacePoint> allOriginalSurfacePoints(allPoints.size());

    // Recollect surface points with proper indexing
    size_t pointIdx = 0;
    for (uint32_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
        if (faces[faceIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) continue;

        uint32_t he0 = faces[faceIdx].halfEdgeIdx;
        uint32_t he1 = halfEdges[he0].next;
        uint32_t he2 = halfEdges[he1].next;

        if (he0 == HalfEdgeMesh::INVALID_INDEX || he1 == HalfEdgeMesh::INVALID_INDEX || he2 == HalfEdgeMesh::INVALID_INDEX) continue;

        // Get surface points for each edge
        auto sp0 = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(he0);
        auto sp1 = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(he1);
        auto sp2 = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(he2);

        if (sp0.empty() || sp1.empty() || sp2.empty()) continue;

        // Store surface points
        for (const auto& sp : sp0) {
            if (pointIdx < allOriginalSurfacePoints.size()) {
                allOriginalSurfacePoints[pointIdx++] = sp;
            }
        }
        for (const auto& sp : sp1) {
            if (pointIdx < allOriginalSurfacePoints.size()) {
                allOriginalSurfacePoints[pointIdx++] = sp;
            }
        }
        for (const auto& sp : sp2) {
            if (pointIdx < allOriginalSurfacePoints.size()) {
                allOriginalSurfacePoints[pointIdx++] = sp;
            }
        }
    }

    for (const auto& fpData : faceData) {
        glm::vec3 color = faceColors[fpData.faceIdx];

        // Collect point indices and their surface points
        std::vector<GeodesicTracer::SurfacePoint> allSurfacePoints;
        std::vector<uint32_t> allPointIndices;

        // Get points for each edge
        for (size_t edgeIdx = 0; edgeIdx < 3; ++edgeIdx) {
            const auto& edgeIndices = fpData.edgePointIndices[edgeIdx];

            for (size_t i = 0; i < edgeIndices.size(); ++i) {
                if (edgeIdx < 2 || i < edgeIndices.size() - 1) { // Skip last point of last edge
                    size_t origIdx = edgeIndices[i];
                    if (origIdx < allOriginalSurfacePoints.size()) {
                        allSurfacePoints.push_back(allOriginalSurfacePoints[origIdx]);
                        allPointIndices.push_back(pointMapping[origIdx]);
                    }
                }
            }
        }

        // Add boundary points to all adjacent faces
        std::map<uint32_t, std::vector<size_t>> pointsByInputFace;
        auto& inputConn = inputMesh.getConnectivity();

        for (size_t i = 0; i < allSurfacePoints.size(); ++i) {
            std::vector<uint32_t> adjacentFaces;

            if (allSurfacePoints[i].type == GeodesicTracer::SurfacePoint::Type::FACE) {
                // Point is inside a face
                adjacentFaces.push_back(allSurfacePoints[i].elementId);
            }
            else if (allSurfacePoints[i].type == GeodesicTracer::SurfacePoint::Type::EDGE) {
                // Add both adjacent faces if point is on an edge
                uint32_t edgeId = allSurfacePoints[i].elementId;
                uint32_t he = inputConn.getEdges()[edgeId].halfEdgeIdx;
                if (he != INVALID_INDEX) {
                    uint32_t face1 = inputConn.getHalfEdges()[he].face;
                    if (face1 != INVALID_INDEX) {
                        adjacentFaces.push_back(face1);
                    }

                    uint32_t oppositeHe = inputConn.getHalfEdges()[he].opposite;
                    if (oppositeHe != INVALID_INDEX) {
                        uint32_t face2 = inputConn.getHalfEdges()[oppositeHe].face;
                        if (face2 != INVALID_INDEX) {
                            adjacentFaces.push_back(face2);
                        }
                    }
                }
            }
            else if (allSurfacePoints[i].type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
                // Add all adjacent faces if point is on a vertex
                uint32_t vertId = allSurfacePoints[i].elementId;
                auto vertexHalfedges = inputConn.getVertexHalfEdges(vertId);
                for (uint32_t he : vertexHalfedges) {
                    uint32_t faceId = inputConn.getHalfEdges()[he].face;
                    if (faceId != INVALID_INDEX) {
                        adjacentFaces.push_back(faceId);
                    }
                }
            }

            // Add this point to all adjacent faces
            for (uint32_t faceId : adjacentFaces) {
                pointsByInputFace[faceId].push_back(i);
            }
        }

        // For each input face, triangulate the points within it
        for (const auto& [inputFaceId, pointIndices] : pointsByInputFace) {
            if (pointIndices.size() < 3)
                continue;

            // Simple fan triangulation from first point
            uint32_t anchor = allPointIndices[pointIndices[0]];
            glm::vec3 anchorPos = mergedPoints[anchor];

            for (size_t i = 1; i + 1 < pointIndices.size(); ++i) {
                uint32_t idx1 = allPointIndices[pointIndices[i]];
                uint32_t idx2 = allPointIndices[pointIndices[i + 1]];

                if (anchor == idx1 || idx1 == idx2 || idx2 == anchor)
                    continue;

                glm::vec3 p0 = anchorPos;
                glm::vec3 p1 = mergedPoints[idx1];
                glm::vec3 p2 = mergedPoints[idx2];

                glm::vec3 edge1 = p1 - p0;
                glm::vec3 edge2 = p2 - p0;
                glm::vec3 normal = glm::cross(edge1, edge2);
                float area = glm::length(normal);

                if (area < 1e-8f) continue;
                normal = glm::normalize(normal);

                uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

                Vertex v0, v1, v2;
                v0.pos = p0;
                v1.pos = p1;
                v2.pos = p2;

                v0.normal = v1.normal = v2.normal = normal;
                v0.color = v1.color = v2.color = color;
                v0.texCoord = v1.texCoord = v2.texCoord = glm::vec2(0.0f);

                vertices.push_back(v0);
                vertices.push_back(v1);
                vertices.push_back(v2);

                indices.push_back(baseIdx);
                indices.push_back(baseIdx + 1);
                indices.push_back(baseIdx + 2);
            }
        }
    }

    // Update the common subdivision model
    overlayModel.setVertices(vertices);
    overlayModel.setIndices(indices);
    overlayModel.recreateBuffers();
}

std::vector<glm::vec3> iODT::getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const {
    std::vector<glm::vec3> polyline;

    // Trace the intrinsic halfedge to get surface points
    auto pathPoints = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(intrinsicHalfedgeIdx);

    // Convert surface points to 3D positions
    for (const auto& sp : pathPoints) {
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

    // Write face elements (indices come in triplets for triangles)
    uint32_t faceCount = 0;
    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 < indices.size()) {
            // OBJ uses 1-based indexing
            file << "f " << (indices[i] + 1) << " " << (indices[i + 1] + 1) << " " << (indices[i + 2] + 1) << "\n";
            faceCount++;
        }
    }

    file.close();
    std::cout << "[saveIntrinsicOverlayOBJ] Saved " << vertices.size() << " vertices and " << faceCount << " faces to " << filename << std::endl;
}