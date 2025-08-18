#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>
#include <iostream>

#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"

GeodesicTracer::GeodesicTracer(SignpostMesh& mesh) : mesh(mesh) {
}

GeodesicTracer::GeodesicTraceResult GeodesicTracer::traceFromVertex( uint32_t vertexIdx, uint32_t refFace, const glm::dvec2& dirInRefFace, double remaining, GeodesicTraceResult& baseResult, double totalLength) const
{
    const auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();

#ifndef NDEBUG
    std::cout << "[traceFromVertex] START vertex=" << vertexIdx
        << " refFace=" << refFace
        << " remaining=" << remaining
        << " dirInRef=(" << dirInRefFace.x << "," << dirInRefFace.y << ")\n";
#endif

    GeodesicTraceResult fail = baseResult;
    if (remaining <= 1e-12) return fail;

    auto angleCCW = [](const glm::dvec2& a, const glm::dvec2& b) {
        double cr = a.x * b.y - a.y * b.x;
        double dt = a.x * b.x + a.y * b.y;
        double ang = std::atan2(cr, dt); // (-pi, pi]
        if (ang < 0.0) ang += 2.0 * glm::pi<double>();
        return ang; // [0, 2pi)
        };
    auto unitSafe = [](glm::dvec2 v) {
        double L = glm::length(v);
        return (L > 0.0) ? (v / L) : glm::dvec2(1.0, 0.0);
        };
    auto headOf = [&](uint32_t he) { return halfEdges[halfEdges[he].next].origin; };

    // Reference face geometry at the vertex 
    auto triRef = mesh.layoutTriangle(refFace);
    int lRef = -1;
    for (int k = 0; k < 3; ++k) if (triRef.indices[k] == vertexIdx) { lRef = k; break; }
    if (lRef < 0) {
#ifndef NDEBUG
        std::cout << "[traceFromVertex] vertex not in refFace -> terminate\n";
#endif
        return fail;
    }

    int lNext = (lRef + 1) % 3;                  // neighbor “next” in face indexing
    int lPrev = (lRef + 2) % 3;                  // neighbor “prev” in face indexing
    uint32_t v = triRef.indices[lRef];
    uint32_t vNext = triRef.indices[lNext];
    uint32_t vPrev = triRef.indices[lPrev];

    glm::dvec2 pV = triRef.vertices[lRef];
    glm::dvec2 pNext = triRef.vertices[lNext];
    glm::dvec2 pPrev = triRef.vertices[lPrev];

    glm::dvec2 rayPrev = unitSafe(pPrev - pV);   // v -> prev
    glm::dvec2 rayNext = unitSafe(pNext - pV);   // v -> next
    glm::dvec2 dirRef = unitSafe(dirInRefFace);

    // Compute raw CCW angle and normalize to the interior corner (<= pi)
    double raw = angleCCW(rayPrev, rayNext);     // could be > pi in chart
    glm::dvec2 startRay, endRay;
    uint32_t  startNbr, endNbr;
    double    alpha;                             // interior corner

    if (raw <= glm::pi<double>()) {
        // interior wedge is CCW from prev -> next
        startRay = rayPrev;   endRay = rayNext;
        startNbr = vPrev;     endNbr = vNext;
        alpha = raw;
    }
    else {
        // interior wedge is CCW from next -> prev (swap)
        startRay = rayNext;   endRay = rayPrev;
        startNbr = vNext;     endNbr = vPrev;
        alpha = 2.0 * glm::pi<double>() - raw;
    }

    double theta = angleCCW(startRay, dirRef);   // measure from the interior start ray
#ifndef NDEBUG
    std::cout << "[traceFromVertex] raw=" << raw
        << " alpha=" << alpha
        << " theta=" << theta
        << " startNbr=" << startNbr
        << " endNbr=" << endNbr << "\n";
#endif

    // If the direction lies in the interior wedge of refFace, continue in that direction
    if (theta <= alpha + 1e-14) {
#ifndef NDEBUG
        std::cout << "[traceFromVertex] chosenFace=refFace (direction in normalized interior wedge)\n";
#endif
        constexpr double TINY = 1e-9;
        glm::dvec3 startBary(0.0);
        startBary[lRef] = 1.0 - 2.0 * TINY;
        startBary[lNext] = TINY;
        startBary[lPrev] = TINY;

        auto cont = traceFromFace(refFace, startBary, dirRef, remaining);

        double baseTraveled = totalLength - remaining;
        cont.distance += baseTraveled;
        cont.steps.insert(cont.steps.begin(), baseResult.steps.begin(), baseResult.steps.end());
        return cont;
    }

    // Else rotate past this corner by theta - alpha and move to the face across 
    double residual = theta - alpha;
    uint32_t currFace = refFace;
    // Crossing boundary
    uint32_t currCommonNeighbor = endNbr;  
    glm::dvec2 dirLocal = dirRef;

    auto findFaceHEonUndirectedEdge = [&](uint32_t face, uint32_t a, uint32_t b)->uint32_t {
        auto fHEs = conn.getFaceHalfEdges(face);
        for (uint32_t he : fHEs) {
            uint32_t o = halfEdges[he].origin;
            uint32_t h = headOf(he);
            if ((o == a && h == b) || (o == b && h == a)) return he;
        }
        return HalfEdgeMesh::INVALID_INDEX;
        };

    for (int guard = 0; guard < 1000; ++guard) {
        // Move to the face across the edge (v, currCommonNeighbor)
        uint32_t heOnCurr = findFaceHEonUndirectedEdge(currFace, v, currCommonNeighbor);
        if (heOnCurr == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
            std::cout << "[traceFromVertex] missing HE on (v," << currCommonNeighbor << ") -> terminate\n";
#endif
            return fail;
        }
        uint32_t opp = halfEdges[heOnCurr].opposite;
        if (opp == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
            std::cout << "[traceFromVertex] boundary encountered while rotating around vertex -> terminate\n";
#endif
            return fail;
        }
        uint32_t nextFace = halfEdges[opp].face;

        // Transport the direction into nextFace
        dirLocal = rotateVectorAcrossEdge(mesh, currFace, heOnCurr, nextFace, opp, dirLocal);

        // Build wedge at (nextFace, v)
        auto triN = mesh.layoutTriangle(nextFace);
        int lNv = -1; for (int k = 0; k < 3; ++k) if (triN.indices[k] == v) { lNv = k; break; }
        int lShared = -1; for (int k = 0; k < 3; ++k) if (triN.indices[k] == currCommonNeighbor) { lShared = k; break; }
        if (lNv < 0 || lShared < 0) { 
#ifndef NDEBUG
            std::cout << "[traceFromVertex] topology error\n"; 
#endif
            return fail; 
        }
        int lOther = 3 - lNv - lShared;

        glm::dvec2 pVn = triN.vertices[lNv];
        glm::dvec2 pSh = triN.vertices[lShared];
        glm::dvec2 pOt = triN.vertices[lOther];

        glm::dvec2 rayShared = unitSafe(pSh - pVn);
        glm::dvec2 rayOther = unitSafe(pOt - pVn);

        // Normalize this corner to its interior (<= pi) but keep the start as the shared edge by measuring theta from rayShared
        double rawN = angleCCW(rayShared, rayOther);
        double alphaN = (rawN <= glm::pi<double>()) ? rawN : (2.0 * glm::pi<double>() - rawN);
        double thetaN = angleCCW(rayShared, unitSafe(dirLocal));

#ifndef NDEBUG
        std::cout << "[traceFromVertex] walk: currFace=" << currFace
            << " -> nextFace=" << nextFace
            << " residual=" << residual
            << " rawN=" << rawN
            << " alphaN=" << alphaN
            << " thetaN=" << thetaN << "\n";
#endif

        if (residual <= alphaN + 1e-14) {
            // The direction falls in this next face
            constexpr double TINY = 1e-9;
            glm::dvec3 startBary(0.0);
            startBary[lNv] = 1.0 - 2.0 * TINY;
            startBary[lShared] = TINY;
            startBary[lOther] = TINY;

            auto cont = traceFromFace(nextFace, startBary, unitSafe(dirLocal), remaining);

            double baseTraveled = totalLength - remaining;
            cont.distance += baseTraveled;
            cont.steps.insert(cont.steps.begin(), baseResult.steps.begin(), baseResult.steps.end());
            return cont;
        }

        // Not yet; consume this corner and advance around v:
        residual -= alphaN;
        currFace = nextFace;
        currCommonNeighbor = triN.indices[lOther]; // cross the OTHER edge next
    }

#ifndef NDEBUG
    std::cout << "[traceFromVertex] guard limit reached -> terminate\n";
#endif
    return fail;
}

GeodesicTracer::GeodesicTraceResult GeodesicTracer::traceFromFace(uint32_t startFaceIdx, const glm::dvec3& startBary, const glm::dvec2& cartesianDir, double length) const
{
#ifndef NDEBUG
    std::cout << "[traceFromFace] START: face=" << startFaceIdx
        << " bary=(" << startBary.x << "," << startBary.y << "," << startBary.z << ")"
        << " dir=(" << cartesianDir.x << "," << cartesianDir.y << ")"
        << " length=" << length << std::endl;
#endif

    GeodesicTraceResult result;
    result.success = false;
    result.position3D = glm::vec3(0.0f);
    result.baryCoords = startBary;
    result.distance = 0.0;
    result.finalFaceIdx = HalfEdgeMesh::INVALID_INDEX;

    if (length <= 1e-12 || glm::length(cartesianDir) < 1e-12) {
        result.success = true;
        result.distance = 0.0;

        SurfacePoint sp;
        sp.type = SurfacePoint::Type::FACE;
        sp.elementId = startFaceIdx;
        sp.baryCoords = startBary;

        result.position3D = evaluateSurfacePoint(sp);
        result.baryCoords = startBary;
        result.finalFaceIdx = startFaceIdx;
        return result;
    }

    // Initialize state
    double remaining = length;
    uint32_t currFace = startFaceIdx;
    glm::dvec2 dir2D = glm::normalize(cartesianDir);

#ifndef NDEBUG
    std::cout << "[traceFromFace] normalized dir2D=(" << dir2D.x << "," << dir2D.y << ")" << std::endl;
#endif

    // Create initial surface point
    SurfacePoint currPoint;
    currPoint.type = SurfacePoint::Type::FACE;
    currPoint.elementId = currFace;
    currPoint.baryCoords = startBary;

    const auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    const auto& edgesRef = conn.getEdges();

    // Tolerance for remaining distance
    constexpr double EPS_REMAIN = 1e-12;
    constexpr double EDGE_SNAP_MIN = 1e-9; 

    // Parameters for vertex continuation
    constexpr double FORWARD_EPS = 1e-6;
    constexpr double BARY_MOVE_TOL = 1e-4;
    constexpr double TINY = 1e-9;

    // Main loop
    for (int iter = 0; iter < options.maxIters && remaining > EPS_REMAIN; ++iter) {
#ifndef NDEBUG
        std::cout << "[traceFromFace] iter=" << iter << " currFace=" << currFace
            << " remaining=" << remaining << std::endl;
#endif

        // Take one step across the current triangle
        FaceStepResult step = traceInFace(currPoint, dir2D, remaining);
        result.steps.push_back(step);

        if (!step.success) {
#ifndef NDEBUG
            std::cout << "[traceFromFace] traceInFace failed at iter=" << iter << std::endl;
#endif
            break;
        }

        // Update remaining distance
        remaining -= step.distanceTraveled;

        // Use the final barycentric coordinates from the step
        glm::dvec3 bary = step.finalBary;

        // Handle vertex hit
        if (step.hitVertex) {
#ifndef NDEBUG
            std::cout << "[traceFromFace] step hit vertex=" << step.vertexIdx
                << " at t=" << step.distanceTraveled
                << " remaining=" << remaining << std::endl;
#endif

            // Trace end point on vertex
            if (remaining <= EPS_REMAIN) {
                SurfacePoint vertexExit;
                vertexExit.type = SurfacePoint::Type::VERTEX;
                vertexExit.elementId = step.vertexIdx;

                result.success = true;
                result.finalFaceIdx = currFace;
                result.distance = length - remaining;
                result.baryCoords = step.finalBary;
                result.exitPoint = vertexExit;
                result.position3D = glm::vec3(evaluateSurfacePoint(vertexExit));
#ifndef NDEBUG
                std::cout << "[traceFromFace] evaluating position directly \n" << std::endl;
#endif
                return result;
            }

            // Find correct direction to trace from vertex
            return traceFromVertex(step.vertexIdx, currFace, dir2D, remaining, result, length);
        }

        // Handle inside face
        if (!step.hitEdge) {
            // Trace end point inside face
            if (remaining <= EPS_REMAIN) {

                // thresholds (tune as needed)
                constexpr double SNAP_BARY = 1e-2;
                constexpr double VERTEX_SNAP_FRAC = 1e-3;
                constexpr double EDGE_SNAP_FRAC = 1e-2;
                constexpr double MIN_EDGE_LEN = 1e-12;

                // Validate bary coords 
                glm::dvec3 faceB = step.finalBary;
                bool baryOk = std::isfinite(faceB.x) && std::isfinite(faceB.y) && std::isfinite(faceB.z)
                    && std::abs(faceB.x) < 1e6 && std::abs(faceB.y) < 1e6 && std::abs(faceB.z) < 1e6;

                // If bary is garbage, clamp and renormalize 
                if (!baryOk) {
#ifndef NDEBUG
                    std::cout << "[traceFromFace] WARNING: invalid barycentrics, sanitizing\n";
#endif
                    faceB.x = std::max(0.0, faceB.x);
                    faceB.y = std::max(0.0, faceB.y);
                    faceB.z = std::max(0.0, faceB.z);
                    double s = faceB.x + faceB.y + faceB.z;
                    if (s <= 0.0) { faceB = glm::dvec3(1.0 / 3.0); }
                    else faceB /= s;
                }

                // Layout triangle in chart and get 2D point
                auto tri2D = mesh.layoutTriangle(currFace);
                glm::dvec2 V0 = tri2D.vertices[0], V1 = tri2D.vertices[1], V2 = tri2D.vertices[2];
                glm::dvec2 s2D = V0 * faceB.x + V1 * faceB.y + V2 * faceB.z;

                // compute local edge lengths and average for scale aware thresholds
                double L01 = glm::length(V1 - V0);
                double L12 = glm::length(V2 - V1);
                double L20 = glm::length(V0 - V2);
                double avgEdge = (L01 + L12 + L20) / 3.0;
                if (avgEdge < MIN_EDGE_LEN) avgEdge = MIN_EDGE_LEN;

                // 1) Vertex snap
                double d0 = glm::length(s2D - V0);
                double d1 = glm::length(s2D - V1);
                double d2 = glm::length(s2D - V2);
                double vertexThresh = VERTEX_SNAP_FRAC * avgEdge;

                if (d0 <= vertexThresh || d1 <= vertexThresh || d2 <= vertexThresh) {
                    uint32_t vIdx;
                    auto fhe = conn.getFaceHalfEdges(currFace);
                    if (d0 <= vertexThresh) vIdx = conn.getHalfEdges()[fhe[0]].origin;
                    else if (d1 <= vertexThresh) vIdx = conn.getHalfEdges()[fhe[1]].origin;
                    else vIdx = conn.getHalfEdges()[fhe[2]].origin;

#ifndef NDEBUG
                    std::cout << "[traceFromFace] Vertex snap: min=" << std::min(std::min(d0, d1), d2)
                        << " <= " << vertexThresh << " -> vertex " << vIdx << "\n";
#endif

                    SurfacePoint vertexExit;
                    vertexExit.type = SurfacePoint::Type::VERTEX;
                    vertexExit.elementId = vIdx;

                    result.success = true;
                    result.finalFaceIdx = currFace;
                    result.distance = length - remaining;
                    result.baryCoords = faceB;
                    result.exitPoint = vertexExit;
                    result.position3D = glm::vec3(evaluateSurfacePoint(vertexExit));
                    return result;
                }

                // 2) Edge snap: Calculate distance to each segment and pick nearest if within threshold
                auto distPointToSegment = [](const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b) {
                    glm::dvec2 ab = b - a; double ab2 = glm::dot(ab, ab);
                    if (ab2 == 0.0) return std::pair<double, double>(glm::length(p - a), 0.0);
                    double t = glm::dot(p - a, ab) / ab2;
                    t = std::max(0.0, std::min(1.0, t));
                    glm::dvec2 proj = a + t * ab;
                    return std::pair<double, double>(glm::length(p - proj), t);
                    };

                auto e0 = distPointToSegment(s2D, V0, V1);
                auto e1 = distPointToSegment(s2D, V1, V2);
                auto e2 = distPointToSegment(s2D, V2, V0);

                // Choose nearest edge
                double minDist = e0.first; int bestEdge = 0; double bestT = e0.second; double bestEdgeLen = L01;
                if (e1.first < minDist) { minDist = e1.first; bestEdge = 1; bestT = e1.second; bestEdgeLen = L12; }
                if (e2.first < minDist) { minDist = e2.first; bestEdge = 2; bestT = e2.second; bestEdgeLen = L20; }

                double edgeThresh = EDGE_SNAP_FRAC * bestEdgeLen;

                if (minDist <= edgeThresh && bestEdgeLen > MIN_EDGE_LEN) {
                    // Map bestEdge to halfedge index on this face 
                    auto faceHEs = conn.getFaceHalfEdges(currFace);
                    uint32_t heOnEdge = faceHEs[bestEdge];
                    uint32_t edgeIdx = conn.getEdgeIndexFromHalfEdge(heOnEdge);

                    // Compute canonical halfedge and canonical endpoints
                    uint32_t canonicalHe = edgesRef[edgeIdx].halfEdgeIdx;
                    uint32_t vA = 0u, vB = 0u;
                    if (canonicalHe != HalfEdgeMesh::INVALID_INDEX) {
                        uint32_t oppCanon = halfEdges[canonicalHe].opposite;
                        vA = halfEdges[canonicalHe].origin;
                        vB = (oppCanon != HalfEdgeMesh::INVALID_INDEX) ? halfEdges[oppCanon].origin : halfEdges[canonicalHe].origin;
                    }
                    else {
                        // fallback to face-local + opposite if no canonical halfedge available
                        vA = halfEdges[heOnEdge].origin;
                        uint32_t opp = halfEdges[heOnEdge].opposite;
                        if (opp != HalfEdgeMesh::INVALID_INDEX) vB = halfEdges[opp].origin;
                        else {
                            uint32_t nextHe = halfEdges[heOnEdge].next;
                            vB = (nextHe != HalfEdgeMesh::INVALID_INDEX) ? halfEdges[nextHe].origin : vA;
                        }
                    }

                    // canonical split (flip if canonical exists and orientation differs)
                    double face_t = bestT;
                    double splitCanon = face_t;
                    if (canonicalHe != HalfEdgeMesh::INVALID_INDEX) {
                        if (halfEdges[heOnEdge].origin != halfEdges[canonicalHe].origin) splitCanon = 1.0 - face_t;
                    }

                    // Build canonical EDGE SurfacePoint: bary = (1-t, t, 0) where bary.y == t
                    SurfacePoint edgeExit;
                    edgeExit.type = SurfacePoint::Type::EDGE;
                    edgeExit.elementId = edgeIdx;
                    edgeExit.baryCoords = glm::dvec3(1.0 - splitCanon, splitCanon, 0.0);
                    edgeExit.split = splitCanon;

                    // Compute 3D position directly from canonical endpoints (unambiguous)
                    const auto& vertsRef = conn.getVertices();
                    glm::dvec3 pA = glm::dvec3(vertsRef[vA].position);
                    glm::dvec3 pB = glm::dvec3(vertsRef[vB].position);
                    glm::dvec3 pEdge = (1.0 - splitCanon) * pA + splitCanon * pB;

#ifndef NDEBUG
                    std::cout << "[traceFromFace] Edge snap: dist=" << minDist << " <= " << edgeThresh
                        << " ; edge=" << edgeIdx << " t(face)=" << bestT << " t(canonical)=" << splitCanon
                        << " endpoints=(" << vA << "," << vB << ")\n";
#endif

                    result.success = true;
                    result.finalFaceIdx = currFace;
                    result.distance = length - remaining;
                    result.baryCoords = faceB;
                    result.exitPoint = edgeExit;
                    result.position3D = glm::vec3(pEdge); // Canonical interpolation
                    return result;
                }

                // FACE exit
#ifndef NDEBUG
                std::cout << "[traceFromFace] FACE exit with remaining distance=" << remaining << "\n";
#endif
                result.success = true;
                result.finalFaceIdx = currFace;
                result.distance = length - remaining;
                result.baryCoords = faceB;

                SurfacePoint exitPoint;
                exitPoint.type = SurfacePoint::Type::FACE;
                exitPoint.elementId = currFace;
                exitPoint.baryCoords = faceB;
                result.exitPoint = exitPoint;
                result.position3D = glm::vec3(evaluateSurfacePoint(exitPoint));

#ifndef NDEBUG
                std::cout << "[traceFromFace] Exit point of type=FACE with bary="
                    << exitPoint.baryCoords.x << "," << exitPoint.baryCoords.y << "," << exitPoint.baryCoords.z << ")" << std::endl;
#endif

                return result;
            }
            else {
                // Continue tracing even if wrongly stopped inside face
#ifndef NDEBUG
                std::cout << "[traceFromFace] ended inside face but remaining > 0, continuing from bary\n";
#endif
                currPoint.elementId = currFace;
                currPoint.baryCoords = bary;
                continue;
            }
        }

        // hitEdge == true
        // Build exit EDGE surface point 
        uint32_t edgeIdx = conn.getEdgeIndexFromHalfEdge(step.halfEdgeIdx);

        // canonical endpoints for the hit edge
        double face_t = step.edgeParam;
        uint32_t canonicalHe = edgesRef[edgeIdx].halfEdgeIdx;
        uint32_t vA = 0u, vB = 0u;

        if (canonicalHe != HalfEdgeMesh::INVALID_INDEX) {
            uint32_t oppCanon = halfEdges[canonicalHe].opposite;
            vA = halfEdges[canonicalHe].origin;
            vB = (oppCanon != HalfEdgeMesh::INVALID_INDEX) ? halfEdges[oppCanon].origin : halfEdges[canonicalHe].origin;
        }
        else {
            // fallback: derive from step.halfEdgeIdx/opposite/next
            vA = halfEdges[step.halfEdgeIdx].origin;
            uint32_t opp = halfEdges[step.halfEdgeIdx].opposite;
            if (opp != HalfEdgeMesh::INVALID_INDEX) vB = halfEdges[opp].origin;
            else {
                uint32_t nextHe = halfEdges[step.halfEdgeIdx].next;
                vB = (nextHe != HalfEdgeMesh::INVALID_INDEX) ? halfEdges[nextHe].origin : vA;
            }
        }

        // Canonical split
        double splitCanon = face_t;
        if (canonicalHe != HalfEdgeMesh::INVALID_INDEX) {
            if (halfEdges[step.halfEdgeIdx].origin != halfEdges[canonicalHe].origin) splitCanon = 1.0 - face_t;
        }

        // Build EDGE SurfacePoint in canonical bary form
        SurfacePoint edgeExit;
        edgeExit.type = SurfacePoint::Type::EDGE;
        edgeExit.elementId = edgeIdx;
        edgeExit.baryCoords = glm::dvec3(1.0 - splitCanon, splitCanon, 0.0);
        edgeExit.split = splitCanon;

        // compute canonical 3D point
        const auto& vertsRef = conn.getVertices();
        glm::dvec3 pA = glm::dvec3(vertsRef[vA].position);
        glm::dvec3 pB = glm::dvec3(vertsRef[vB].position);
        glm::dvec3 pEdge = (1.0 - splitCanon) * pA + splitCanon * pB;

        // Trace end point lies on edge
        if (remaining <= EPS_REMAIN) {
#ifndef NDEBUG
            std::cout << "[traceFromFace] EDGE exit at HE=" << step.halfEdgeIdx << " (Edge=" << edgeIdx << ") bary=(x="
                << bary.x << ",y=" << bary.y << ",z=" << bary.z << ")\n";
#endif
            result.success = true;
            result.finalFaceIdx = currFace;
            result.distance = length - remaining;
            result.baryCoords = bary;
            result.exitPoint = edgeExit;
            result.position3D = glm::vec3(pEdge);
#ifndef NDEBUG
            std::cout << "[traceFromFace] Trace hit fraction =" << splitCanon << std::endl;
#endif
            return result;
        }

        // Check if remaining distance is very small and snap to edge instead of crossing
        if (remaining <= EDGE_SNAP_MIN) {
#ifndef NDEBUG
            std::cout << "[traceFromFace] EDGE SNAP: remaining=" << remaining
                << " < threshold=" << EDGE_SNAP_MIN
                << ", snapping to edge instead of crossing\n";
#endif

            result.success = true;
            result.finalFaceIdx = currFace;
            result.distance = length - remaining;
            result.baryCoords = bary;
            result.exitPoint = edgeExit;
            result.position3D = glm::vec3(pEdge);
            return result;
        }

        // Ege crossing 
        uint32_t oppositeHE = halfEdges[step.halfEdgeIdx].opposite;
        if (oppositeHE == HalfEdgeMesh::INVALID_INDEX) {
            // Hit boundary 
#ifndef NDEBUG
            std::cout << "[traceFromFace] Hit boundary at edge " << edgeIdx << std::endl;
#endif
            result.success = false;
            result.baryCoords = bary;
            result.position3D = glm::vec3(pEdge);
            result.distance = length - remaining;
            return result;
        }

        uint32_t nextFace = halfEdges[oppositeHE].face;
        if (nextFace == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
            std::cout << "[traceFromFace] Invalid next face from oppositeHE=" << oppositeHE << std::endl;
#endif
            result.success = false;
            result.baryCoords = bary;
            result.position3D = glm::vec3(pEdge);
            result.distance = length - remaining;
            return result;
        }

#ifndef NDEBUG
        std::cout << "[traceFromFace] crossing to nextFace=" << nextFace
            << " via edge=" << edgeIdx << " and remaining=" << remaining << std::endl;
#endif

        // Transport direction using vector rotation
        dir2D = rotateVectorAcrossEdge(mesh,
            currFace, step.halfEdgeIdx,
            nextFace, oppositeHE,
            dir2D);

#ifndef NDEBUG
        std::cout << "[traceFromFace] dir transported to next face=("
            << dir2D.x << "," << dir2D.y << ")" << std::endl;
#endif

        // Reproject point into next face's chart
        auto tri2D = mesh.layoutTriangle(currFace);
        glm::dvec2 exit2D = tri2D.vertices[0] * bary.x +
            tri2D.vertices[1] * bary.y +
            tri2D.vertices[2] * bary.z;

        glm::dvec2 next2D = chartLocal2D(mesh, currFace, nextFace, exit2D);

#ifndef NDEBUG
        std::cout << "[traceFromFace] reprojected point: ("
            << exit2D.x << "," << exit2D.y << ") -> ("
            << next2D.x << "," << next2D.y << ")" << std::endl;
#endif

        // Convert to barycentric in next face
        auto nextTri2D = mesh.layoutTriangle(nextFace);
        glm::dvec3 nextBary = mesh.computeBarycentric2D(
            next2D,
            nextTri2D.vertices[0], nextTri2D.vertices[1], nextTri2D.vertices[2]
        );

#ifndef NDEBUG
        std::cout << "[traceFromFace] next bary=("
            << nextBary.x << "," << nextBary.y << "," << nextBary.z << ")" << std::endl;
#endif

        currFace = nextFace;
        currPoint.elementId = currFace;
        currPoint.baryCoords = nextBary;
    }

#ifndef NDEBUG
    std::cout << "[traceFromFace] FAILED: reached max iterations or error" << std::endl;
#endif

    // Fallback - return current position
    result.success = false;
    result.baryCoords = currPoint.baryCoords;
    result.position3D = glm::vec3(evaluateSurfacePoint(currPoint));
    result.distance = length - remaining;

    return result;
}

GeodesicTracer::FaceStepResult GeodesicTracer::traceInFace(const SurfacePoint& start, const glm::dvec2& dir2D, double maxLength) const {
    FaceStepResult result;
    result.success = false;
    result.hitEdge = false;
    result.hitVertex = false;
    result.distanceTraveled = 0.0;
    result.halfEdgeIdx = INVALID_INDEX;
    result.vertexIdx = INVALID_INDEX;
    result.localEdgeIndex = -1;
    result.edgeParam = 0.0;

    if (start.type != SurfacePoint::Type::FACE) {
        return result;
    }

    auto triangle2D = mesh.layoutTriangle(start.elementId);
    const glm::dvec2* V = triangle2D.vertices;

    // DEBUG: SHOULD REMOVE SOON (kept as in original)
    {
        const auto& conn = mesh.getConnectivity();
        auto faceHEs_conn = conn.getFaceHalfEdges(start.elementId);

#ifndef NDEBUG
        std::cout << "[TraceInFace] face=" << start.elementId << "  chart coords:\n";
        for (int i = 0; i < 3; ++i) {
            std::cout << "    layout V" << i << ": idx=" << triangle2D.indices[i]
                << "  chart=(" << V[i].x << "," << V[i].y << ")\n";
        }
#endif

        uint32_t anchor = conn.getFaces()[start.elementId].halfEdgeIdx;
#ifndef NDEBUG
        std::cout << "[TraceInFace] face anchor he=" << anchor << "\n";
#endif
        uint32_t he = anchor;
        for (int i = 0; i < 3; ++i) {
            if (he == INVALID_INDEX) break;
#ifndef NDEBUG
            std::cout << "    conn he[" << i << "]=" << he
                << " origin=" << conn.getHalfEdges()[he].origin
                << " face=" << conn.getHalfEdges()[he].face
                << " next=" << conn.getHalfEdges()[he].next << "\n";
#endif
            he = conn.getHalfEdges()[he].next;
        }

#ifndef NDEBUG
        std::cout << "[TraceInFace] getFaceHalfEdges returned: ";
        for (auto fh : faceHEs_conn) std::cout << fh << "(" << conn.getHalfEdges()[fh].origin << ") ";
        std::cout << std::endl;
#endif
    }

    // Convert start bary to 2D point
    glm::dvec2 start2D = V[0] * start.baryCoords.x + V[1] * start.baryCoords.y + V[2] * start.baryCoords.z;

#ifndef NDEBUG
    std::cout << "[TraceInFace] 2D start point=(" << start2D.x << "," << start2D.y << ")\n";
#endif

    const double U_EPS = 1e-8;
    const double T_EPS = 1e-8;
    const double VERT_EPS = 1e-6;

    // Nudge slightly along direction to avoid exact-corner starts
    glm::dvec2 nudgedStart2D = start2D + dir2D * T_EPS;
    glm::dvec3 nudgedBary = mesh.computeBarycentric2D(nudgedStart2D, V[0], V[1], V[2]);

    double bestT = std::numeric_limits<double>::infinity();
    double bestU = std::numeric_limits<double>::infinity();
    int bestEdge = -1;

    // Track recent vertex hit (u approx 0 or 1)
    double bestVertexT = std::numeric_limits<double>::infinity();
    int bestVertexLocal = -1; // local index 0/1/2 for which vertex is hit

    // Test each edge for intersection but skip corner edges
    for (int i = 0; i < 3; ++i) {
        int a = i;
        int b = (i + 1) % 3;
        int opp = (i + 2) % 3;

        // 1) If trace starts on vertex a or b then skip the edge
        if (nudgedBary[a] > 1.0 - T_EPS || nudgedBary[b] > 1.0 - T_EPS)
            continue;

        // 2) Skip opposite edge too (start is effectively on that edge)
        if (nudgedBary[opp] < T_EPS)
            continue;

        glm::dvec2 P = V[i];
        glm::dvec2 Q = V[(i + 1) % 3];
        glm::dvec2 edgeVec = Q - P;
        glm::dvec2 bb = P - nudgedStart2D;

        double t, u;
        if (!solveRayEdge(dir2D, edgeVec, bb, t, u))
            continue;

        if (!(t > T_EPS && t < maxLength + T_EPS))
            continue; // intersection is behind or beyond maxLength

        // interior-edge crossing (strict)
        if (u > U_EPS && u < 1.0 - U_EPS) {
            if (t < bestT) {
                bestT = t;
                bestEdge = i;
                bestU = u;
            }
        }
        else {
            // endpoint: candidate vertex hit
            int whichVertexLocal = -1;
            if (u <= U_EPS) whichVertexLocal = a;          // hits P
            else if (u >= 1.0 - U_EPS) whichVertexLocal = b; // hits Q

            if (whichVertexLocal != -1) {
#ifndef NDEBUG
                std::cout << "[TraceInFace] Edge endpoint vertex hit candidate: local_idx=" << whichVertexLocal
                    << " u=" << u << " t=" << t << " global_idx=" << triangle2D.indices[whichVertexLocal] << "\n";
#endif
                if (t < bestVertexT) {
                    bestVertexT = t;
                    bestVertexLocal = whichVertexLocal;
                }
            }
        }
    }

    // Interior edge crossing found
    if (bestEdge >= 0 && bestT <= maxLength + T_EPS) {
        glm::dvec2 exit2D = start2D + dir2D * bestT;
        glm::dvec2 end2D = start2D + dir2D * maxLength;
        result.finalBary = mesh.computeBarycentric2D(exit2D, V[0], V[1], V[2]);
        result.distanceTraveled = bestT;
        result.hitEdge = true;
        result.localEdgeIndex = bestEdge;
        result.edgeParam = bestU;
#ifndef NDEBUG
        std::cout  << "[TraceInFace] 2D edge hit=(" << exit2D.x << "," << exit2D.y << ")\n";
        std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;
#endif
        // Map to halfedge index
        const auto& conn = mesh.getConnectivity();
        auto faceHEs = conn.getFaceHalfEdges(start.elementId);
        if (bestEdge < (int)faceHEs.size()) {
            result.halfEdgeIdx = faceHEs[bestEdge];
        }
        else {
            result.halfEdgeIdx = INVALID_INDEX;
        }

        // Also detect vertex-in-edge endpoints (rare here because u was interior), but keep safety:
        int maybeVertex = -1;
        for (int vi = 0; vi < 3; ++vi) {
            if (result.finalBary[vi] > 1.0 - VERT_EPS) {
                maybeVertex = vi;
                break;
            }
        }
        if (maybeVertex != -1) {
            result.hitVertex = true;
            result.vertexIdx = triangle2D.indices[maybeVertex];
        }
        else {
            result.hitVertex = false;
            result.vertexIdx = INVALID_INDEX;
        }

        result.success = true;
        result.dir2D = dir2D;
        return result;
    }

    // Return vertex hit candidate
    if (bestVertexLocal != -1 && bestVertexT <= maxLength + T_EPS) {
        glm::dvec2 vertex2D = start2D + dir2D * bestVertexT;
        glm::dvec2 end2D = start2D + dir2D * maxLength;
        // set barycentric to that vertex exactly
        glm::dvec3 vb(0.0);
        vb[bestVertexLocal] = 1.0;
        result.finalBary = vb;
        result.distanceTraveled = bestVertexT;
        result.hitEdge = false;
        result.hitVertex = true;
        result.vertexIdx = triangle2D.indices[bestVertexLocal];
        result.halfEdgeIdx = INVALID_INDEX;

        result.success = true;
        result.dir2D = dir2D;
#ifndef NDEBUG
        std::cout << "[traceInFace] 2D vertex hit=(" << vertex2D.x << "," << vertex2D.y << ")" << " Idx=" << result.vertexIdx <<  " t=" << bestVertexT << "\n";
        std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;
#endif
        return result;
    }

    // Since no edge/vertex within maxLength then trace ends inside face after full length
    {
        glm::dvec2 end2D = start2D + dir2D * maxLength;
        result.finalBary = mesh.computeBarycentric2D(end2D, V[0], V[1], V[2]);
        result.distanceTraveled = maxLength;
        result.hitEdge = false;
        result.hitVertex = false;
#ifndef NDEBUG
        std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;
        std::cout << "[traceInFace] final bary:(" << result.finalBary.x << "," << result.finalBary.y << "," << result.finalBary.z << ")" << std::endl;
#endif
        // detect if the computed final bary is numerically at a vertex
        // First check if barycentric coordinates are valid (non-negative and sum to ~1)
        double barySum = result.finalBary.x + result.finalBary.y + result.finalBary.z;
        bool validBary = barySum >= 0.99 && barySum <= 1.01 &&
            result.finalBary.x >= -VERT_EPS &&
            result.finalBary.y >= -VERT_EPS &&
            result.finalBary.z >= -VERT_EPS;

        if (validBary) {
            for (int vi = 0; vi < 3; ++vi) {
                if (result.finalBary[vi] > 1.0 - VERT_EPS) {
#ifndef NDEBUG
                    std::cout << "[TraceInFace] Final bary vertex hit: local_idx=" << vi
                        << " bary=" << result.finalBary[vi]
                        << " global_idx=" << triangle2D.indices[vi] << "\n";
#endif
                    result.hitVertex = true;
                    result.vertexIdx = triangle2D.indices[vi];
                    break;
                }
            }
        }

        result.success = true;
        result.dir2D = dir2D;
        return result;
    }
}

glm::dvec3 GeodesicTracer::evaluateSurfacePoint(const SurfacePoint& point) const {
    const auto& conn = mesh.getConnectivity();
    const auto& halfEs = conn.getHalfEdges();
    const auto& edges = conn.getEdges();
    const auto& verts = conn.getVertices();
    const auto& faces = conn.getFaces();

    switch (point.type) {
    case SurfacePoint::Type::VERTEX: {
        uint32_t vIdx = point.elementId;
        if (vIdx >= verts.size()) {
#ifndef NDEBUG
            std::cerr << "[evaluateSurfacePoint] VERTEX: invalid index " << vIdx << "\n";
#endif
                return glm::dvec3(0.0);
        }
        return glm::dvec3(verts[vIdx].position);
    }
    case SurfacePoint::Type::EDGE: {
        uint32_t eIdx = point.elementId;
        if (eIdx >= edges.size()) {
#ifndef NDEBUG
            std::cerr << "[evaluateSurfacePoint] EDGE: invalid edge index " << eIdx << "\n";
#endif
                return glm::dvec3(0.0);
        }
        // get halfedges
        uint32_t he0 = edges[eIdx].halfEdgeIdx;
        uint32_t he1 = halfEs[he0].opposite;
        uint32_t vA = halfEs[he0].origin;
        uint32_t vB = (he1 != SignpostMesh::INVALID_INDEX
            ? halfEs[he1].origin
            : halfEs[halfEs[he0].next].origin);
        // baryCoords = (1-t, t, 0)
        double t = point.baryCoords.y;
        return glm::mix(verts[vA].position, verts[vB].position, float(t));
    }
    case SurfacePoint::Type::FACE: {
        uint32_t fIdx = point.elementId;
        if (fIdx >= faces.size()) {
#ifndef NDEBUG
            std::cerr << "[evaluateSurfacePoint] FACE: invalid face index " << fIdx << "\n";
#endif
                return glm::dvec3(0.0);
        }
        auto fv = conn.getFaceVertices(fIdx);
        if (fv.size() != 3) {
#ifndef NDEBUG
            std::cerr << "[evaluateSurfacePoint] FACE: non-triangular face\n";
#endif
                return glm::dvec3(0.0);
        }
        double x = point.baryCoords.x;
        double y = point.baryCoords.y;
        double z = point.baryCoords.z;
        glm::dvec3 P0 = glm::dvec3(verts[fv[0]].position);
        glm::dvec3 P1 = glm::dvec3(verts[fv[1]].position);
        glm::dvec3 P2 = glm::dvec3(verts[fv[2]].position);
        return x * P0 + y * P1 + z * P2;
    }
    }
#ifndef NDEBUG
    std::cerr << "[evaluateSurfacePoint] unknown SurfacePoint type\n";
#endif
        return glm::dvec3(0.0);
}

glm::dvec2 GeodesicTracer::chartLocal2D(const SignpostMesh& mesh, uint32_t oldFaceIdx, uint32_t newFaceIdx, const glm::dvec2& oldPoint2D) const {
    // ONLY MAPS ADJACENT FACES

    const double EPS = 1e-12;
    auto oldTri = mesh.layoutTriangle(oldFaceIdx);
    auto newTri = mesh.layoutTriangle(newFaceIdx);

    // Bary in old chart
    glm::dvec3 baryOld = mesh.computeBarycentric2D(oldPoint2D, oldTri.vertices[0], oldTri.vertices[1], oldTri.vertices[2]);

    // Find shared vertices between old 2d triangle and new 2d triangle
    int sharedOldIdx[2] = { -1,-1 };
    int sharedNewIdx[2] = { -1,-1 };
    int sharedCount = 0;
    for (int i = 0; i < 3; ++i) {
        uint32_t oldVID = oldTri.indices[i];
        for (int j = 0; j < 3; ++j) {
            if (newTri.indices[j] == oldVID) {
                if (sharedCount < 2) {
                    sharedOldIdx[sharedCount] = i;
                    sharedNewIdx[sharedCount] = j;
                }
                ++sharedCount;
            }
        }
    }

    glm::dvec3 baryNew(0.0);

    if (sharedCount == 2) {
        // Calculate u along edge from old bary (A to B)
        int iA = sharedOldIdx[0], iB = sharedOldIdx[1];
        int jA = sharedNewIdx[0], jB = sharedNewIdx[1];
        double wA = baryOld[iA], wB = baryOld[iB];
        double sum = wA + wB;

        double u;
        if (sum > EPS) {
            // Parameter from A to B
            u = wB / sum;               
        }
        else {
            // Fallback: project oldPoint2D onto old edge if degenerate
            glm::dvec2 vA = oldTri.vertices[iA], vB = oldTri.vertices[iB];
            glm::dvec2 e = vB - vA;
            double denom = glm::dot(e, e);
            u = (denom < EPS) ? 0.5 : glm::dot(oldPoint2D - vA, e) / denom;
            u = glm::clamp(u, 0.0, 1.0);
        }

        // Assign bary in new face with respect to neighbor orientation
        baryNew[jA] = 1.0 - u;
        baryNew[jB] = u;
        // Third corner remains 0
    }
    else {
        // Fallback: map by matching vertex ids
        for (int i = 0; i < 3; ++i) {
            uint32_t oldVID = oldTri.indices[i];
            for (int j = 0; j < 3; ++j) {
                if (newTri.indices[j] == oldVID) {
                    baryNew[j] = baryOld[i];
                    break;
                }
            }
        }
    }

    // Reproject to new chart coords
    glm::dvec2 chart = newTri.vertices[0] * baryNew.x
        + newTri.vertices[1] * baryNew.y
        + newTri.vertices[2] * baryNew.z;
    return chart;
}

glm::dvec2 GeodesicTracer::rotateVectorAcrossEdge(const SignpostMesh& mesh, uint32_t oldFace, uint32_t oldHe, uint32_t newFace, uint32_t newHe, const glm::dvec2& vecInOld) const
{
    const auto& hvf = mesh.getHalfedgeVectorsInFace();

    glm::dvec2 eOld = glm::normalize(hvf[oldHe]);
    // Flip opposite direction
    glm::dvec2 eNew = glm::normalize(-hvf[newHe]); 

    double cosTheta = glm::dot(eOld, eNew);
    double sinTheta = eOld.x * eNew.y - eOld.y * eNew.x;

    // Build rotation 
    // GLM's glm::dmat2(a,b,c,d) matrix stores as column major (a,b and c,d), using scalar format instead
    glm::dvec2 mappedVec;
    mappedVec.x = cosTheta * vecInOld.x - sinTheta * vecInOld.y;
    mappedVec.y = sinTheta * vecInOld.x + cosTheta * vecInOld.y;
    return mappedVec;
}

bool GeodesicTracer::solveRayEdge(const glm::dvec2& rayDir, const glm::dvec2& edgeVec, const glm::dvec2& b, double& out_t, double& out_u) const
{
    double det = rayDir.x * (-edgeVec.y) - rayDir.y * (-edgeVec.x);

#ifndef NDEBUG
    std::cout << "[solveRayEdge] rayDir=(" << rayDir.x << "," << rayDir.y
        << ") edgeVec=(" << edgeVec.x << "," << edgeVec.y
        << ") b=(" << b.x << "," << b.y << ") det=" << det << std::endl;
#endif

    if (std::abs(det) < 1e-12) {
#ifndef NDEBUG
        std::cout << "[solveRayEdge] parallel/degenerate, returning false" << std::endl;
#endif
        return false;
    }

    out_t = (b.y * edgeVec.x - b.x * edgeVec.y) / det;
    out_u = (rayDir.x * b.y - rayDir.y * b.x) / det;

#ifndef NDEBUG
    std::cout << "[solveRayEdge] result: t=" << out_t << " u=" << out_u << std::endl;
#endif

    return true;
}

bool GeodesicTracer::resolveVertexLinear(uint32_t newV, const SurfacePoint& location, uint32_t heA, uint32_t heB) {
    auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    auto& vertices = conn.getVertices();

    // For VERTEX type, copy the position
    if (location.type == SurfacePoint::Type::VERTEX) {
        if (location.elementId < vertices.size()) {
            vertices[newV].position = vertices[location.elementId].position;
            return true;
        }
        return false;
    }

    // For EDGE type, interpolate between endpoints
    if (location.type == SurfacePoint::Type::EDGE) {
        if (heA != INVALID_INDEX && heB != INVALID_INDEX) {
            uint32_t origV0 = halfEdges[heA].origin;
            uint32_t origV1 = halfEdges[halfEdges[heB].opposite].origin;
            double t = location.baryCoords.y;

            vertices[newV].position = glm::mix(
                vertices[origV0].position,
                vertices[origV1].position,
                float(t)
            );
            return true;
        }
        // Fallback to direct evaluation
        vertices[newV].position = glm::vec3(evaluateSurfacePoint(location));
        return true;
    }

    // Fallback
    vertices[newV].position = glm::vec3(evaluateSurfacePoint(location));
    return true;
}

bool GeodesicTracer::resolveVertex(uint32_t startFace, uint32_t startHe, const GeodesicTraceResult& result, glm::dvec3& outPos3D) {
    auto& conn = mesh.getConnectivity();
    auto& verts = conn.getVertices();
    const auto& halfEdges = conn.getHalfEdges();

    // If no edges crossed, get final position directly
    bool anyCrossed = false;
    for (auto const& step : result.steps) {
        if (step.success && step.hitEdge) { 
            anyCrossed = true; 
            break; 
        }
    }
    if (!anyCrossed) {
        outPos3D = glm::vec3(result.position3D);
        return true;
    }

    // Use exitPoint if available
    uint32_t intrinsicFace = result.finalFaceIdx;
    glm::dvec3 intrinsicBary = result.baryCoords;
    if (result.exitPoint.type == SurfacePoint::Type::EDGE ||
        result.exitPoint.type == SurfacePoint::Type::FACE) {
        intrinsicFace = result.exitPoint.elementId;
        intrinsicBary = result.exitPoint.baryCoords;
        if (result.exitPoint.type == SurfacePoint::Type::EDGE) {
            uint32_t edgeIdx = result.exitPoint.elementId;
            const auto& edges = conn.getEdges();
            uint32_t he = edges[edgeIdx].halfEdgeIdx;
            intrinsicFace = halfEdges[he].face;
            if (intrinsicFace == HalfEdgeMesh::INVALID_INDEX) {
                uint32_t opp = halfEdges[he].opposite;
                if (opp != HalfEdgeMesh::INVALID_INDEX)
                    intrinsicFace = halfEdges[opp].face;
            }
        }
    }

    if (intrinsicFace == HalfEdgeMesh::INVALID_INDEX) {
#ifndef NDEBUG
        std::cout << "[resolveVertex] intrinsic face is invalid, evaluating directly" << std::endl;
#endif
        outPos3D = glm::vec3(evaluateSurfacePoint(SurfacePoint(intrinsicFace, intrinsicBary)));
        return true;
    }

    // Safely reject close corner insertions
    double maxBary = std::max(std::max(intrinsicBary.x, intrinsicBary.y), intrinsicBary.z);
    const double BARY_REJECT = 0.98;
    if (maxBary >= BARY_REJECT) {
#ifndef NDEBUG
        std::cout << "[resolveVertex] Insertion too close to corner, rejecting \n";
#endif
        return false; 
    }

    // Layout 2d triangle
    auto intrinsicTri2D = mesh.layoutTriangle(intrinsicFace);

    // Reject insertions too close to an existing vertex
    {
        glm::dvec2 P0 = intrinsicTri2D.vertices[0];
        glm::dvec2 P1 = intrinsicTri2D.vertices[1];
        glm::dvec2 P2 = intrinsicTri2D.vertices[2];
        glm::dvec2 ip = P0 * intrinsicBary.x + P1 * intrinsicBary.y + P2 * intrinsicBary.z;

        // Average edge length of target face
        double L01 = glm::length(P1 - P0);
        double L12 = glm::length(P2 - P1);
        double L20 = glm::length(P0 - P2);
        double avgLength = (L01 + L12 + L20) / 3.0;

        // Distance to nearest vertex
        double d0 = glm::length(ip - P0);
        double d1 = glm::length(ip - P1);
        double d2 = glm::length(ip - P2);
        double dminVert = std::min(std::min(d0, d1), d2);    

        // Threshold
        const double VERT_FRACTION = 0.4;   // 4% of average length

        if (dminVert < VERT_FRACTION * avgLength) {
#ifndef NDEBUG
            std::cout << "[resolveVertex] Insertion too close to an existing vertex, rejecting \n";
#endif
            return false;
        }
    }

    // Step 1: Layout intrinsic face and compute intrinsic direction 
#ifndef NDEBUG
    std::cout << "[resolveVertex] intrinsicFace=" << intrinsicFace
        << " intrinsicBary=(" << intrinsicBary.x << "," << intrinsicBary.y << "," << intrinsicBary.z << ")\n";
#endif
    glm::dvec2 intrinsicPoint2D =
        intrinsicTri2D.vertices[0] * intrinsicBary.x +
        intrinsicTri2D.vertices[1] * intrinsicBary.y +
        intrinsicTri2D.vertices[2] * intrinsicBary.z;

    // Choose a corner to start from (smallest bary coord)
    int traceCorner = 0;
    double smallest = intrinsicBary.x;

    if (intrinsicBary.y < smallest) { 
        smallest = intrinsicBary.y; 
        traceCorner = 1; 
    }
    if (intrinsicBary.z < smallest) { 
        smallest = intrinsicBary.z; 
        traceCorner = 2; 
    }

    auto faceVerts = conn.getFaceVertices(intrinsicFace);
    if (faceVerts.size() != 3) {
        outPos3D = glm::vec3(evaluateSurfacePoint(
            SurfacePoint(intrinsicFace, intrinsicBary)));
        return true;
    }

    uint32_t traceVertexIdx = faceVerts[traceCorner];

    glm::dvec2 intrinsicStart2D = intrinsicTri2D.vertices[traceCorner];
    glm::dvec2 intrinsicVec2D = intrinsicPoint2D - intrinsicStart2D;
    double intrinsicLen = glm::length(intrinsicVec2D);

    if (intrinsicLen < 1e-12) {
        // At the vertex already
        outPos3D = verts[traceVertexIdx].position;
        return true;
    }

    // Step 2: Pick a starting face and rotate vector into it 
    auto intrinsicFaceHEs = conn.getFaceHalfEdges(intrinsicFace);
    uint32_t startHeForTrace = intrinsicFaceHEs[traceCorner];
    uint32_t startFaceForTrace = halfEdges[startHeForTrace].face;
    if (startFaceForTrace == HalfEdgeMesh::INVALID_INDEX) {
        uint32_t opp = halfEdges[startHeForTrace].opposite;
        if (opp == HalfEdgeMesh::INVALID_INDEX) {
            outPos3D = glm::vec3(evaluateSurfacePoint(
                SurfacePoint(intrinsicFace, intrinsicBary)));
            return true;
        }
        startFaceForTrace = halfEdges[opp].face;
    }

    glm::dvec2 faceDir2D = intrinsicVec2D / intrinsicLen; // normalized in intrinsic chart

    // If starting face != intrinsicFace, rotate vector across the edge
    if (startFaceForTrace != intrinsicFace) {
        uint32_t oppHE = halfEdges[startHeForTrace].opposite;
        faceDir2D = rotateVectorAcrossEdge(mesh,
            intrinsicFace, startHeForTrace,
            startFaceForTrace, oppHE,
            faceDir2D);
    }

    // Step 3: Build nudged barycentric start in the chosen face 
    double eps = 1e-6;
    auto startFaceHEs = conn.getFaceHalfEdges(startFaceForTrace);
    int localCorner = 0;
    for (int i = 0; i < (int)startFaceHEs.size(); ++i) {
        if (startFaceHEs[i] == startHeForTrace) { localCorner = i; break; }
    }
    glm::dvec3 startBary(0.0);
    startBary[localCorner] = 1.0 - 2.0 * eps;
    startBary[(localCorner + 1) % 3] = eps;
    startBary[(localCorner + 2) % 3] = eps;


    // Step 4: Now call the extrinsic tracer and use the intrinsic length so the geodesic distance matches the intrinsic trace
#ifndef NDEBUG
    std::cout << "[resolveVertex] calling extrinsic trace on start face=" << startFaceForTrace
        << " start bary=(" << startBary.x << "," << startBary.y << "," << startBary.z << ") "
        << "length=" << intrinsicLen << std::endl;
#endif
    auto extrinsicTrace = traceFromFace(startFaceForTrace,
        startBary,
        faceDir2D,
        intrinsicLen);

    if (extrinsicTrace.success) {
        outPos3D = extrinsicTrace.position3D;
        return true;
    }

    // Fallback: replay intrinsic steps and evaluate directly ---
    {
        SurfacePoint startSP(startFace, startBary);
        glm::dvec3 pos3D = evaluateSurfacePoint(startSP);
        uint32_t currFace = startFace;

        for (size_t i = 0; i < result.steps.size(); ++i) {
            const FaceStepResult& step = result.steps[i];
            if (!step.success) break;

            SurfacePoint faceSP(currFace, step.finalBary);
            pos3D = evaluateSurfacePoint(faceSP);

            if (!step.hitEdge) {
                outPos3D = glm::vec3(pos3D);
                return true;
            }

            uint32_t oppHE = halfEdges[step.halfEdgeIdx].opposite;
            if (oppHE == HalfEdgeMesh::INVALID_INDEX) {
                outPos3D = glm::vec3(pos3D);
                return true;
            }
            uint32_t nextFace = halfEdges[oppHE].face;
            if (nextFace == HalfEdgeMesh::INVALID_INDEX) {
                outPos3D = glm::vec3(pos3D);
                return true;
            }
            currFace = nextFace;
        }

        outPos3D = glm::vec3(evaluateSurfacePoint(
            SurfacePoint(intrinsicFace, intrinsicBary)));
        return true;
    }
}

glm::dvec2 GeodesicTracer::computeTangentVector(uint32_t startVertex, const SurfacePoint& target) {
    const auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    const auto& halfedgeVectors = mesh.getHalfedgeVectorsInVertex();
    const auto& edges = conn.getEdges();

    if (target.type == SurfacePoint::Type::EDGE) {
        uint32_t eid = target.elementId;
        if (eid >= edges.size()) return glm::dvec2(0.0);

        // Look for the halfedge around startVertex *on* that edge
        for (uint32_t he : conn.getVertexHalfEdges(startVertex)) {
            if (conn.getEdgeIndexFromHalfEdge(he) == eid) {
                // t is fraction along the edge, L is the intrinsic length
                double t = target.baryCoords.y;
                double L = halfEdges[he].intrinsicLength;
                // build tangent in *vertex* chart, scaled by true arc-length
                return halfedgeVectors[he] * (t * L);
            }
        }
    }

    if (target.type == SurfacePoint::Type::FACE) {
        uint32_t faceIdx = target.elementId;

        // Find halfedge from startVertex that belongs to this face
        uint32_t faceHE = INVALID_INDEX;
        for (uint32_t he : conn.getVertexHalfEdges(startVertex)) {
            if (halfEdges[he].face == faceIdx) {
                faceHE = he;
                break;
            }
        }

        if (faceHE != INVALID_INDEX) {
            // barycentric weights on the two edges emanating from startVertex
            // in that triangle
            auto faceHEs = conn.getFaceHalfEdges(faceIdx);
            int idx = -1;
            for (int i = 0; i < 3; ++i) {
                if (halfEdges[faceHEs[i]].origin == startVertex) {
                    idx = i;
                    break;
                }
            }
            if (idx >= 0) {
                uint32_t heOutA = faceHEs[(idx + 1) % 3];
                uint32_t heOutB = faceHEs[(idx + 2) % 3];
                double wA = target.baryCoords[(idx + 1) % 3];
                double wB = target.baryCoords[(idx + 2) % 3];
                double LA = halfEdges[heOutA].intrinsicLength;
                double LB = halfEdges[heOutB].intrinsicLength;

                // blend the vertex chart vectors
                glm::dvec2 dir = wA * halfedgeVectors[heOutA] * LA
                    + wB * halfedgeVectors[heOutB] * LB;
                if (wA + wB > 1e-12) dir /= (wA + wB);
                return dir;
            }
        }
    }

    return glm::dvec2(0.0);
}

uint32_t GeodesicTracer::findStartingHalfEdge(uint32_t startVertex, uint32_t faceIdx, const glm::dvec2& tangentVector) const {
    auto& conn = mesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();
    auto& halfedgeVectors = mesh.getHalfedgeVectorsInVertex();
    auto faceHEs = conn.getFaceHalfEdges(faceIdx);

    glm::dvec2 dir = glm::normalize(tangentVector);
    double   best = -2;
    uint32_t bestHE = INVALID_INDEX;

    for (uint32_t he : faceHEs) {
        if (halfEdges[he].origin != startVertex) continue;
        glm::dvec2 v = halfedgeVectors[he];
        if (glm::length(v) < 1e-12) continue;
        v = glm::normalize(v);
        double d = glm::dot(v, dir);
        if (d > best) {
            best = d;
            bestHE = he;
        }
    }
    return bestHE;
}