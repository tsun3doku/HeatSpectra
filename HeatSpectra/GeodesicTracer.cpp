#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <complex>
#include <cmath>
#include <limits>
#include <iostream>

#include "GeodesicTracer.hpp"
#include "SignPostMesh.hpp"

GeodesicTracer::GeodesicTracer(SignpostMesh& mesh) : mesh(mesh) {
}

static inline double cross2d(const glm::dvec2& a, const glm::dvec2& b) {
    return a.x * b.y - a.y * b.x;
}

GeodesicTracer::GeodesicTraceResult GeodesicTracer::traceFromVertex(uint32_t vertexIdx, uint32_t refFace, const glm::dvec2& dirInRefVertex,
    double remaining, GeodesicTraceResult& baseResult, double totalLength) const
{
    const auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    const auto& hvv_vert = mesh.getHalfedgeVectorsInVertex();
    const auto& hvf_face = mesh.getHalfedgeVectorsInFace();
    const auto& verts = conn.getVertices();
    const auto& vAngleSums = mesh.getVertexAngleSums();

    GeodesicTraceResult fail = baseResult;
    if (remaining <= 1e-12)
        return fail;

    glm::dvec2 dirVertex = glm::normalize(dirInRefVertex);

    // Bounds check for vertex index
    const auto& vertices = conn.getVertices();
    if (vertexIdx >= vertices.size()) {
        //std::cout << "[traceFromVertex] ERROR: vertexIdx " << vertexIdx << " out of bounds (size=" << vertices.size() << ")\n";
        return fail;
    }

    uint32_t firstHe = vertices[vertexIdx].halfEdgeIdx;
    if (firstHe == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[traceFromVertex] vertex has no halfedge\n";
        return fail;
    }

    uint32_t currHe = firstHe;
    uint32_t wedgeHe = HalfEdgeMesh::INVALID_INDEX;
    double minCross = 1e9;
    double WEDGE_EPS= 1e-8;
    uint32_t minCrossHe = HalfEdgeMesh::INVALID_INDEX;

    const auto& HEs = conn.getHalfEdges();
    int guard = 0;
    do {
        // Standardized traversal (next->next->opposite)
        uint32_t next1 = HEs[currHe].next;
        if (next1 == HalfEdgeMesh::INVALID_INDEX) 
            break;
        uint32_t next2 = HEs[next1].next;
        if (next2 == HalfEdgeMesh::INVALID_INDEX) 
            break;
        uint32_t nextHe = HEs[next2].opposite;
        if (nextHe == HalfEdgeMesh::INVALID_INDEX) 
            break;

        glm::dvec2 vecA = glm::normalize(hvv_vert[currHe]);
        glm::dvec2 vecB = glm::normalize(hvv_vert[nextHe]);

        double crossA = cross2d(vecA, dirVertex);
        double crossB = cross2d(vecB, dirVertex);

        // Wedge test
        if (crossA > 0.0 && crossB <= WEDGE_EPS) {
            wedgeHe = currHe;
            break;
        }

        // numeric fallback: remember closest
        double ca = std::abs(crossA);
        if (ca < minCross) { minCross = ca; minCrossHe = currHe; }
        double cb = std::abs(crossB);
        if (cb < minCross) { minCross = cb; minCrossHe = nextHe; }

        currHe = nextHe;
        if (++guard > 100) break;
    } while (currHe != firstHe);

    if (wedgeHe == HalfEdgeMesh::INVALID_INDEX) {
        wedgeHe = minCrossHe; // numeric fallback
    }

    if (wedgeHe == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[traceFromVertex] No wedge found\n";
        return fail;
    }

    //std::cout << "[traceFromVertex] wedgeHe=" << wedgeHe << " wedgeVec=(" << hvv_vert[wedgeHe].x << "," << hvv_vert[wedgeHe].y << ")" << " wedgeLen=" << glm::length(hvv_vert[wedgeHe]) << "\n";

    uint32_t startFace = halfEdges[wedgeHe].face;
    //std::cout << "[traceFromVertex] startFace=" << startFace << "\n";

    // vertex chart unit start direction 
    glm::dvec2 intervalStart = glm::normalize(hvv_vert[wedgeHe]);

    // Power scaling based on vertex angle sum 
    bool isBoundaryVertex = false;
    uint32_t probe = wedgeHe;
    do {
        if (halfEdges[probe].opposite == HalfEdgeMesh::INVALID_INDEX) {
            isBoundaryVertex = true;
            break;
        }
        // Standardized traversal 
        uint32_t next1 = halfEdges[probe].next;
        if (next1 == HalfEdgeMesh::INVALID_INDEX) { isBoundaryVertex = true; break; }
        uint32_t next2 = halfEdges[next1].next;
        if (next2 == HalfEdgeMesh::INVALID_INDEX) { isBoundaryVertex = true; break; }
        probe = halfEdges[next2].opposite;
        if (probe == HalfEdgeMesh::INVALID_INDEX) { isBoundaryVertex = true; break; }
    } while (probe != wedgeHe);

    // Input mesh's expected vertex angle sum
    double targetAngleSum = isBoundaryVertex ? glm::pi<double>() : 2.0 * glm::pi<double>();
    // Intrinsic mesh's vertex angle sum
    double geometricAngleSum = (vertexIdx < vAngleSums.size()) ? vAngleSums[vertexIdx] : targetAngleSum;

    /*
    std::cout << "[traceFromVertex] Angles: vertex " << vertexIdx
        << " geometricAngleSum=" << geometricAngleSum
        << " targetAngleSum=" << targetAngleSum
        << " isBoundaryVertex=" << (isBoundaryVertex ? "YES" : "NO")
        << "\n";
    */

    double power = geometricAngleSum / targetAngleSum;

    // Convert to complex representation for power transform 
    std::complex<double> intervalStartComplex(intervalStart.x, intervalStart.y);
    std::complex<double> dirVertexComplex(dirVertex.x, dirVertex.y);
    
    // Compute relative direction as complex division
    std::complex<double> traceDirRelativeToStart = dirVertexComplex / intervalStartComplex;
    
    // Apply power transform 
    std::complex<double> poweredDir = std::pow(traceDirRelativeToStart, power);
    
    // Convert back to angle
    double newA = std::arg(poweredDir);

    // DEBUG: show result of power scaling
    {
        double angleStart = std::atan2(intervalStart.y, intervalStart.x);
        double angleDir = std::atan2(dirVertex.y, dirVertex.x);
        glm::dvec2 rotatedVertexDir(std::cos(angleStart + newA), std::sin(angleStart + newA));
        double heLen = glm::length(hvv_vert[wedgeHe]);
        glm::dvec2 rotatedVertexVec = rotatedVertexDir * heLen;

        /*
        std::cout << "[traceFromVertex] angleStart=" << angleStart << " angleDir=" << angleDir << "\n";
        std::cout << "  traceDirRelative=(" << traceDirRelativeToStart.real() << "," << traceDirRelativeToStart.imag() << ")\n";
        std::cout << "  power=" << power << " poweredDir=(" << poweredDir.real() << "," << poweredDir.imag() << ")\n";
        std::cout << "  newA=" << newA << " rotatedVertexDir=(" << rotatedVertexDir.x << "," << rotatedVertexDir.y << ")\n";
        */
    }

    // Convert to face coordinates
    glm::dvec2 startDirInFace = glm::normalize(hvf_face[wedgeHe]);
    double startFaceAngle = std::atan2(startDirInFace.y, startDirInFace.x);

    double traceFaceAngle = startFaceAngle + newA;
    glm::dvec2 traceDirInFace(std::cos(traceFaceAngle), std::sin(traceFaceAngle));

    // Set up starting bary at vertex corner
    auto triStart = mesh.layoutTriangle(startFace);
    int lV = -1;

    for (int k = 0; k < 3; ++k) 
        if (triStart.indices[k] == vertexIdx) { 
        lV = k; 
        break; 
    }

    constexpr double TINY = 1e-9;
    glm::dvec3 startBary(0.0);
    if (lV >= 0) {
        startBary[lV] = 1.0 - 2.0 * TINY;
        startBary[(lV + 1) % 3] = TINY;
        startBary[(lV + 2) % 3] = TINY;
    }
    else {
        // Fallback: place at the first corner
        //std::cout << "[traceFromVertex] Fallback start bary" << startFace << std::endl;
        startBary = glm::dvec3(1.0 - 2.0 * TINY, TINY, TINY);
    }

    //std::cout << "[traceFromVertex] power=" << power << " traceFaceAngle=" << traceFaceAngle << "\n";

    auto cont = traceFromFace(startFace, startBary, glm::normalize(traceDirInFace), remaining);
    cont.pathPoints.insert(cont.pathPoints.begin(), baseResult.pathPoints.begin(), baseResult.pathPoints.end());
    cont.steps.insert(cont.steps.begin(), baseResult.steps.begin(), baseResult.steps.end());
    cont.distance += (totalLength - remaining);

    return cont;
}

GeodesicTracer::GeodesicTraceResult GeodesicTracer::traceFromFace(uint32_t startFaceIdx, const glm::dvec3& startBary, const glm::dvec2& cartesianDir, double length) const
{
    /*
    std::cout << "[traceFromFace] START: face=" << startFaceIdx
        << " bary=(" << startBary.x << "," << startBary.y << "," << startBary.z << ")"
        << " dir=(" << cartesianDir.x << "," << cartesianDir.y << ")"
        << " length=" << length << std::endl;
    */

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
        result.pathPoints.push_back(sp);
        return result;
    }

    // Initialize state variables at function scope
    double remaining = length;
    uint32_t currFace = startFaceIdx;
    glm::dvec2 dir2D = glm::normalize(cartesianDir);
    SurfacePoint currPoint;
    currPoint.type = SurfacePoint::Type::FACE;
    currPoint.elementId = currFace;
    currPoint.baryCoords = startBary;

    // Initialize and push start point
    result.pathPoints.clear();
    result.pathPoints.push_back(currPoint);

    //std::cout << "[traceFromFace] normalized dir2D=(" << dir2D.x << "," << dir2D.y << ")" << std::endl;

    const auto& conn = mesh.getConnectivity();
    const auto& halfEdges = conn.getHalfEdges();
    const auto& edgesRef = conn.getEdges();

    // Tolerance for remaining distance
    constexpr double EPS_REMAIN = 1e-12;
    constexpr double VERTEX_SNAP_FRAC = 1e-2;

    // Main loop
    for (int iter = 0; iter < options.maxIters && remaining > EPS_REMAIN; ++iter) {
        //std::cout << "[traceFromFace] iter=" << iter << " currFace=" << currFace << " remaining=" << remaining << std::endl;

        // Take one step across the current triangle
        FaceStepResult step = traceInFace(currPoint, dir2D, remaining);
        result.steps.push_back(step);

        if (!step.success) {
            //std::cout << "[traceFromFace] traceInFace failed at iter=" << iter << std::endl;
            break;
        }

        // Update remaining distance
        remaining -= step.distanceTraveled;

        //std::cout << "[traceFromFace] STEP: distanceTraveled=" << step.distanceTraveled << " remaining=" << remaining << " (was " << (remaining + step.distanceTraveled) << ")\n";

        // Use the final barycentric coordinates from the step
        glm::dvec3 bary = step.finalBary;

        // Handle vertex hit
        if (step.hitVertex) {
            //std::cout << "[traceFromFace] step hit vertex=" << step.vertexIdx << " at t=" << step.distanceTraveled << " remaining=" << remaining << std::endl;

            // Create vertex surface point
            SurfacePoint vertexPoint;
            vertexPoint.type = SurfacePoint::Type::VERTEX;
            vertexPoint.elementId = step.vertexIdx;

            // Add vertex point to path
            result.pathPoints.push_back(vertexPoint);

            // Trace end point on vertex
            if (remaining <= EPS_REMAIN) {
                result.success = true;
                result.finalFaceIdx = currFace;
                result.distance = length - remaining;
                result.baryCoords = step.finalBary;
                result.exitPoint = vertexPoint;
                result.position3D = glm::vec3(evaluateSurfacePoint(vertexPoint));
                //std::cout << "[traceFromFace] evaluating position directly \n" << std::endl;
                return result;
            }

            // Face to vertex direction conversion
            const auto& hvf_face = mesh.getHalfedgeVectorsInFace();   
            const auto& hvv_vert = mesh.getHalfedgeVectorsInVertex();
            const auto& vertices = conn.getVertices();

            // Find the halfedge incoming to the vertex in the current face
            uint32_t inHe = HalfEdgeMesh::INVALID_INDEX;
            auto faceHEs = conn.getFaceHalfEdges(currFace);
            for(size_t i = 0; i < faceHEs.size(); ++i) {
                uint32_t he = faceHEs[i];
                uint32_t nextHe = halfEdges[he].next;
                // The vertex of a corner is the origin of the next halfedge
                if (nextHe != HalfEdgeMesh::INVALID_INDEX && halfEdges[nextHe].origin == step.vertexIdx) {
                    inHe = he;
                    break;
                }
            }

            if (inHe == HalfEdgeMesh::INVALID_INDEX || halfEdges[inHe].opposite == HalfEdgeMesh::INVALID_INDEX) {
                //std::cerr << "[traceFromFace] ERROR: Could not find valid half-edge for vertex " << step.vertexIdx << " in face " << currFace << std::endl;
                return traceFromVertex(step.vertexIdx, currFace, glm::normalize(dir2D), remaining, result, length);
            }
            
            uint32_t outgoingHe = halfEdges[inHe].opposite;

            // Get the basis vectors
            // Face basis: vector along the incoming edge 
            glm::dvec2 faceBase = hvf_face[inHe];
            // Vertex basis: vector along the halfedge that points back to the face 
            glm::dvec2 vertBase = hvv_vert[outgoingHe];         
            
            double fbLen = glm::length(faceBase);
            double vbLen = glm::length(vertBase);
            
            glm::dvec2 dirVertex;
            if (fbLen > 1e-12 && vbLen > 1e-12) {
                // Normalize basis vectors
                faceBase /= fbLen;
                vertBase /= vbLen;
                
                // Calculate the angle of the incoming direction relative to the face's edge vector
                double angFaceBase = std::atan2(faceBase.y, faceBase.x);
                double angDir = std::atan2(dir2D.y, dir2D.x);
                double relAngle = angDir - angFaceBase;
                
                // Apply the relative angle to the vertex's corresponding edge vector
                double angVertBase = std::atan2(vertBase.y, vertBase.x);
                double finalAngle = angVertBase + relAngle + glm::pi<double>();
                
                // Convert back to vector
                dirVertex = glm::dvec2(std::cos(finalAngle), std::sin(finalAngle));
                
                /*
                std::cout << "[traceFromFace] Converted dir: face=(" << dir2D.x << "," << dir2D.y 
                         << ") -> vertex=(" << dirVertex.x << "," << dirVertex.y 
                         << ") using inHe=" << inHe 
                         << " outgoingHe=" << outgoingHe << std::endl;
                */
            } else {
                //std::cout << "[traceFromFace] WARNING: Zero length basis vectors, using normalized dir2D\n";
                dirVertex = glm::normalize(dir2D);
            }

            // Call vertex tracer in vertex's frame direction
            return traceFromVertex(step.vertexIdx, currFace, dirVertex, remaining, result, length);
        }

        // Handle inside face
        if (!step.hitEdge) {
            // Trace end point inside face
            if (remaining <= EPS_REMAIN) {

                // Thresholds for snapping
                constexpr double BARY_SNAP_TOL = 1e-9; 
                constexpr double MIN_EDGE_LEN = 1e-12;

                // Validate bary coords 
                glm::dvec3 faceB = step.finalBary;
                bool baryOk = std::isfinite(faceB.x) && std::isfinite(faceB.y) && std::isfinite(faceB.z)
                    && std::abs(faceB.x) < 1e6 && std::abs(faceB.y) < 1e6 && std::abs(faceB.z) < 1e6;

                // If bary is garbage, clamp and renormalize 
                if (!baryOk) {
                    //std::cout << "[traceFromFace] WARNING: invalid barycentrics, sanitizing\n";
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
                double avgEdge = (glm::length(V1 - V0) + glm::length(V2 - V1) + glm::length(V0 - V2)) / 3.0;
                if (avgEdge < MIN_EDGE_LEN) avgEdge = MIN_EDGE_LEN;

                // 1) Vertex snap
                double d0 = glm::length(s2D - V0);
                double d1 = glm::length(s2D - V1);
                double d2 = glm::length(s2D - V2);
                double vertexThresh = VERTEX_SNAP_FRAC * avgEdge;

                if (d0 <= vertexThresh || d1 <= vertexThresh || d2 <= vertexThresh) {
                    uint32_t vIdx = 0;
                    auto fhe = conn.getFaceHalfEdges(currFace);

                    // Bounds check for face halfedges access
                    bool validVertex = false;
                    if (fhe.size() >= 3) {
                        // Bounds check for halfedges array access
                        const auto& halfEdgesRef = conn.getHalfEdges();
                        if (fhe[0] < halfEdgesRef.size() && fhe[1] < halfEdgesRef.size() && fhe[2] < halfEdgesRef.size()) {
                            if (d0 <= vertexThresh) vIdx = halfEdgesRef[fhe[0]].origin;
                            else if (d1 <= vertexThresh) vIdx = halfEdgesRef[fhe[1]].origin;
                            else vIdx = halfEdgesRef[fhe[2]].origin;
                            validVertex = true;
                        }
                    }

                    if (validVertex) {
                        //std::cout << "[traceFromFace] Vertex snap: min=" << std::min(std::min(d0, d1), d2) << " <= " << vertexThresh << " -> vertex " << vIdx << "\n";

                        SurfacePoint vertexExit;
                        vertexExit.type = SurfacePoint::Type::VERTEX;
                        vertexExit.elementId = vIdx;

                        // VERTEX exit
                        result.success = true;
                        result.finalFaceIdx = currFace;
                        result.distance = length - remaining;
                        result.baryCoords = faceB;
                        result.exitPoint = vertexExit;
                        result.position3D = glm::vec3(evaluateSurfacePoint(vertexExit));
                        result.pathPoints.push_back(vertexExit);
                        return result;
                    }
                    else {
                        //std::cout << "[traceFromFace] Invalid face/halfedge data for vertex snap, falling back to edge/face snap\n";
                    }
                }

                // Find the minimum barycentric coordinate
                double min_bary = faceB.x;
                int min_idx = 0;
                if (faceB.y < min_bary) { min_bary = faceB.y; min_idx = 1; }
                if (faceB.z < min_bary) { min_bary = faceB.z; min_idx = 2; }

                // The edge opposite to the vertex with minimum barycentric coordinate
                int edge_idx = (min_idx + 1) % 3;
                
                // Calculate t from barycentric coordinates
                int vert_p_idx = edge_idx;              // First vertex of the edge
                int vert_q_idx = (edge_idx + 1) % 3;    // Second vertex of the edge
                double lambda_sum = faceB[vert_p_idx] + faceB[vert_q_idx];
                
                // t is the normalized coordinate of the second vertex
                double t = 0.5; // Default to midpoint
                if (lambda_sum > 1e-12) {
                    t = faceB[vert_q_idx] / lambda_sum;
                }
                
                int bestEdge = edge_idx;
                double bestT = t;

                // Check if the smallest barycentric coordinate is near zero
                if (min_bary < BARY_SNAP_TOL) {
                    // Map bestEdge to halfedge index on this face 
                    auto faceHEs = conn.getFaceHalfEdges(currFace);
                    if (bestEdge >= static_cast<int>(faceHEs.size())) {
                        //std::cout << "[traceFromFace] bestEdge=" << bestEdge << " >= faceHEs.size()=" << faceHEs.size() << ", falling back to FACE exit\n";
                    }
                    else {
                        uint32_t heOnEdge = faceHEs[bestEdge];
                        uint32_t edgeIdx = conn.getEdgeFromHalfEdge(heOnEdge);

                        // Check if edge index is valid
                        if (edgeIdx != HalfEdgeMesh::INVALID_INDEX && edgeIdx < edgesRef.size()) {
                            // Compute canonical halfedge and canonical endpoints
                            uint32_t canonicalHe = edgesRef[edgeIdx].halfEdgeIdx;
                            uint32_t vA = 0u, vB = 0u;

                            // Add bounds checking for canonical halfedge
                            if (canonicalHe != HalfEdgeMesh::INVALID_INDEX && canonicalHe < halfEdges.size()) {
                                uint32_t oppCanon = halfEdges[canonicalHe].opposite;
                                vA = halfEdges[canonicalHe].origin;

                                // Bounds check for opposite halfedge
                                if (oppCanon != HalfEdgeMesh::INVALID_INDEX && oppCanon < halfEdges.size()) {
                                    vB = halfEdges[oppCanon].origin;
                                }
                                else {
                                    vB = halfEdges[canonicalHe].origin; // fallback
                                }
                            }
                            else {
                                // fallback to face local if no canonical halfedge 
                                if (heOnEdge < halfEdges.size()) {
                                    vA = halfEdges[heOnEdge].origin;
                                    uint32_t opp = halfEdges[heOnEdge].opposite;
                                    if (opp != HalfEdgeMesh::INVALID_INDEX && opp < halfEdges.size()) {
                                        vB = halfEdges[opp].origin;
                                    }
                                    else {
                                        uint32_t nextHe = halfEdges[heOnEdge].next;
                                        if (nextHe != HalfEdgeMesh::INVALID_INDEX && nextHe < halfEdges.size()) {
                                            vB = halfEdges[nextHe].origin;
                                        }
                                        else {
                                            vB = vA; // fallback
                                        }
                                    }
                                }
                                else {
                                    //std::cout << "[traceFromFace] heOnEdge=" << heOnEdge << " >= halfEdges.size()=" << halfEdges.size() << ", falling back to FACE exit\n";
                                    // Fall back to FACE exit 
                                    goto face_exit;
                                }
                            }

                            // Bounds check for vertices
                            const auto& vertsRef = conn.getVertices();
                            if (vA >= vertsRef.size() || vB >= vertsRef.size()) {
                                //std::cout << "[traceFromFace] Invalid vertex indices: vA=" << vA << " vB=" << vB << " >= vertsRef.size()=" << vertsRef.size() << ", falling back to FACE exit\n";
                                // Fall back to FACE exit
                                goto face_exit;
                            }

                            // Flip if canonical exists and orientation is different
                            double face_t = bestT;
                            double splitCanon = face_t;
                            if (canonicalHe != HalfEdgeMesh::INVALID_INDEX && canonicalHe < halfEdges.size()) {
                                if (heOnEdge < halfEdges.size() && halfEdges[heOnEdge].origin != halfEdges[canonicalHe].origin) {
                                    splitCanon = 1.0 - face_t;
                                }
                            }

                            // Snap to vertex if very close to endpoints 
                            SurfacePoint edgeExit;
                            
                            if (splitCanon < VERTEX_SNAP_FRAC) {
                                edgeExit.type = SurfacePoint::Type::VERTEX;
                                edgeExit.elementId = vA;
                                edgeExit.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
                                edgeExit.split = 0.0;
                            } else if (splitCanon > (1.0 - VERTEX_SNAP_FRAC)) {
                                edgeExit.type = SurfacePoint::Type::VERTEX;
                                edgeExit.elementId = vB;
                                edgeExit.baryCoords = glm::dvec3(0.0, 1.0, 0.0);
                                edgeExit.split = 1.0;
                            } else {
                                edgeExit.type = SurfacePoint::Type::EDGE;
                                edgeExit.elementId = edgeIdx;
                                edgeExit.baryCoords = glm::dvec3(1.0 - splitCanon, splitCanon, 0.0);
                                edgeExit.split = splitCanon;
                            }

                            // Compute 3D position
                            glm::dvec3 pA = glm::dvec3(vertsRef[vA].position);
                            glm::dvec3 pB = glm::dvec3(vertsRef[vB].position);
                            glm::dvec3 pEdge;
                            if (edgeExit.split == 0.0) {
                                pEdge = pA;
                            } else if (edgeExit.split == 1.0) {
                                pEdge = pB;
                            } else {
                                pEdge = (1.0 - splitCanon) * pA + splitCanon * pB;
                            }

                            /*
                            std::cout << "[traceFromFace] Barycentric edge snap: min_bary=" << min_bary << " < " << BARY_SNAP_TOL
                                << " ; edge=" << edgeIdx << " t(face)=" << bestT << " t(canonical)=" << splitCanon
                                << " endpoints=(" << vA << "," << vB << ")\n";
                            */

                            result.success = true;
                            result.finalFaceIdx = currFace;
                            result.distance = length - remaining;
                            result.baryCoords = faceB;
                            result.exitPoint = edgeExit;
                            result.position3D = glm::vec3(pEdge); // Canonical interpolation
                            result.pathPoints.push_back(edgeExit);
                            return result;
                        }
                        else {
                            //std::cout << "[traceFromFace] Invalid edge index " << edgeIdx << " for halfedge " << heOnEdge << ", falling back to FACE exit\n";
                        }
                    }
                }

            // FACE exit
            face_exit:
                //std::cout << "[traceFromFace] FACE exit with remaining distance=" << remaining << "\n";
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
                result.pathPoints.push_back(exitPoint); 

                return result;
            }
        }

        // hitEdge == true
        uint32_t edgeIdx = conn.getEdgeFromHalfEdge(step.halfEdgeIdx);

        if (edgeIdx == HalfEdgeMesh::INVALID_INDEX || edgeIdx >= edgesRef.size()) {
            //std::cout << "[traceFromFace] Invalid edge index " << edgeIdx << " for step halfedge " << step.halfEdgeIdx << ", aborting trace\n";
            result.success = false;
            result.baryCoords = currPoint.baryCoords;
            result.position3D = glm::vec3(evaluateSurfacePoint(currPoint));
            result.distance = length - remaining;
            return result;
        }

        // Canonical endpoints for the hit edge
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

        // Snap to vertex if very close to endpoints 
        SurfacePoint edgeExit;
        
        if (splitCanon < VERTEX_SNAP_FRAC) {
            edgeExit.type = SurfacePoint::Type::VERTEX;
            edgeExit.elementId = vA;
            edgeExit.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
            edgeExit.split = 0.0;
        } else if (splitCanon > (1.0 - VERTEX_SNAP_FRAC)) {
            edgeExit.type = SurfacePoint::Type::VERTEX;
            edgeExit.elementId = vB;
            edgeExit.baryCoords = glm::dvec3(0.0, 1.0, 0.0);
            edgeExit.split = 1.0;
        } else {
            edgeExit.type = SurfacePoint::Type::EDGE;
            edgeExit.elementId = edgeIdx;
            edgeExit.baryCoords = glm::dvec3(1.0 - splitCanon, splitCanon, 0.0);
            edgeExit.split = splitCanon;
        }

        // compute canonical 3D point
        const auto& vertsRef = conn.getVertices();
        glm::dvec3 pA = glm::dvec3(vertsRef[vA].position);
        glm::dvec3 pB = glm::dvec3(vertsRef[vB].position);
        glm::dvec3 pEdge;
        if (edgeExit.split == 0.0) {
            pEdge = pA;
        } else if (edgeExit.split == 1.0) {
            pEdge = pB;
        } else {
            pEdge = (1.0 - splitCanon) * pA + splitCanon * pB;
        }

        // EDGE exit
        if (remaining <= EPS_REMAIN) {
            //std::cout << "[traceFromFace] EDGE exit: remaining=" << remaining << "\n";

            result.success = true;
            result.finalFaceIdx = currFace;
            result.distance = length - remaining;
            result.baryCoords = bary;
            result.exitPoint = edgeExit;
            result.position3D = glm::vec3(pEdge);
            result.pathPoints.push_back(edgeExit);
            return result;
        }

        uint32_t oppositeHE = halfEdges[step.halfEdgeIdx].opposite;
        if (oppositeHE == HalfEdgeMesh::INVALID_INDEX) {
            // Hit boundary 
            //std::cout << "[traceFromFace] Hit boundary at edge " << edgeIdx << std::endl;
            result.success = false;
            result.baryCoords = bary;
            result.position3D = glm::vec3(pEdge);
            result.distance = length - remaining;
            return result;
        }

        uint32_t nextFace = halfEdges[oppositeHE].face;
        if (nextFace == HalfEdgeMesh::INVALID_INDEX) {
            //std::cout << "[traceFromFace] Invalid next face from oppositeHE=" << oppositeHE << std::endl;
            result.success = false;
            result.baryCoords = bary;
            result.position3D = glm::vec3(pEdge);
            result.distance = length - remaining;
            return result;
        }

        //std::cout << "[traceFromFace] crossing to nextFace=" << nextFace << " via edge=" << edgeIdx << " and remaining=" << remaining << std::endl;

        // Add edge crossing point to path
        result.pathPoints.push_back(edgeExit);

        // Transport direction using vector rotation
        dir2D = rotateVectorAcrossEdge(
            currFace, step.halfEdgeIdx,
            nextFace, oppositeHE,
            dir2D);

        //std::cout << "[traceFromFace] dir transported to next face=(" << dir2D.x << "," << dir2D.y << ")" << std::endl;

        // Reproject point into next face's chart
        auto tri2D = mesh.layoutTriangle(currFace);
        glm::dvec2 exit2D = tri2D.vertices[0] * bary.x +
            tri2D.vertices[1] * bary.y +
            tri2D.vertices[2] * bary.z;

        glm::dvec2 next2D = chartLocal2D(currFace, nextFace, exit2D);

        //std::cout << "[traceFromFace] reprojected point: (" << exit2D.x << "," << exit2D.y << ") -> (" << next2D.x << "," << next2D.y << ")" << std::endl;

        // Convert to barycentric in next face
        auto nextTri2D = mesh.layoutTriangle(nextFace);
        glm::dvec3 nextBary = mesh.computeBarycentric2D(
            next2D,
            nextTri2D.vertices[0], nextTri2D.vertices[1], nextTri2D.vertices[2]
        );

        //std::cout << "[traceFromFace] next bary=(" << nextBary.x << "," << nextBary.y << "," << nextBary.z << ")" << std::endl;

        currFace = nextFace;
        currPoint.elementId = currFace;
        currPoint.baryCoords = nextBary;
    }

    //std::cout << "[traceFromFace] FAILED: reached max iterations or error" << std::endl;

    // Add final point to path 
    if (result.pathPoints.empty() || result.pathPoints.back().elementId != currPoint.elementId) {
        result.pathPoints.push_back(currPoint);
    }

    // Fallback: return current position
    result.success = false;
    result.baryCoords = currPoint.baryCoords;
    result.position3D = glm::vec3(evaluateSurfacePoint(currPoint));
    result.distance = length - remaining;
    result.finalFaceIdx = currFace;

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

    // Convert start bary to 2D point
    glm::dvec2 start2D = V[0] * start.baryCoords.x + V[1] * start.baryCoords.y + V[2] * start.baryCoords.z;

    //std::cout << "[TraceInFace] 2D start point=(" << start2D.x << "," << start2D.y << ")\n";

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
        /*
        // 2) Skip opposite edge too (start is effectively on that edge)
        if (nudgedBary[opp] < T_EPS)
            continue;
        */
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
                //std::cout << "[TraceInFace] vertex hit candidate: local_idx=" << whichVertexLocal << " u=" << u << " t=" << t << " global_idx=" << triangle2D.indices[whichVertexLocal] << "\n";

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

        //std::cout << "[TraceInFace] 2D edge hit=(" << exit2D.x << "," << exit2D.y << ")\n";
        //std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;

        // Map to halfedge index
        const auto& conn = mesh.getConnectivity();
        auto faceHEs = conn.getFaceHalfEdges(start.elementId);
        if (bestEdge < (int)faceHEs.size()) {
            result.halfEdgeIdx = faceHEs[bestEdge];
        }
        else {
            result.halfEdgeIdx = INVALID_INDEX;
        }

        // Also detect vertex in edge endpoints 
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
        //std::cout << "[traceInFace] 2D vertex hit=(" << vertex2D.x << "," << vertex2D.y << ")" << " Idx=" << result.vertexIdx << " t=" << bestVertexT << "\n";
        //std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;
        return result;
    }

    // Since no edge/vertex within maxLength then trace ends inside face after full length
    {
        glm::dvec2 end2D = start2D + dir2D * maxLength;
        result.finalBary = mesh.computeBarycentric2D(end2D, V[0], V[1], V[2]);
        result.distanceTraveled = maxLength;
        result.hitEdge = false;
        result.hitVertex = false;

        //std::cout << "[traceInFace] 2D end point:(" << end2D.x << "," << end2D.y << ")" << std::endl;
        //std::cout << "[traceInFace] final bary:(" << result.finalBary.x << "," << result.finalBary.y << "," << result.finalBary.z << ")" << std::endl;

        // detect if the computed final bary is numerically at a vertex
        // First check if barycentric coordinates are valid (non-negative and sum to ~1)
        double barySum = result.finalBary.x + result.finalBary.y + result.finalBary.z;
        bool validBary = barySum >= 0.99 && barySum <= 1.01 &&
            result.finalBary.x >= -VERT_EPS &&
            result.finalBary.y >= -VERT_EPS &&
            result.finalBary.z >= -VERT_EPS;

                if (validBary) {
                    // First check for vertex hits (higher priority than edge hits)
                    for (int vi = 0; vi < 3; ++vi) {
                        if (result.finalBary[vi] > 1.0 - VERT_EPS) {
                            //std::cout << "[TraceInFace] Final bary vertex hit: local_idx=" << vi << " bary=" << result.finalBary[vi] << " global_idx=" << triangle2D.indices[vi] << "\n";

                            result.hitVertex = true;
                            result.vertexIdx = triangle2D.indices[vi];
                            break;
                        }
                    }
                    
                    // If not at a vertex, check for edge proximity using barycentric coordinates
                    if (!result.hitVertex) {
                        double minBary = result.finalBary[0];
                        int minIdx = 0;
                        if (result.finalBary[1] < minBary) { minBary = result.finalBary[1]; minIdx = 1; }
                        if (result.finalBary[2] < minBary) { minBary = result.finalBary[2]; minIdx = 2; }
                        
                        if (minBary < BARY_SNAP_TOL) {
                            // Edge is opposite to vertex with minimum barycentric coordinate
                            int edgeIdx = (minIdx + 1) % 3;
                            // Calculate split parameter from other two coordinates
                            int vp = edgeIdx;  // First vertex of edge
                            int vq = (edgeIdx + 1) % 3;  // Second vertex of edge
                            double edgeSum = result.finalBary[vp] + result.finalBary[vq];
                            double t = (edgeSum > 1e-12) ? result.finalBary[vq] / edgeSum : 0.5;

                            // Set edge hit info
                            result.hitEdge = true;
                            result.localEdgeIndex = edgeIdx;
                            result.edgeParam = t;

                            // Map to halfedge
                            const auto& conn = mesh.getConnectivity();
                            auto faceHEs = conn.getFaceHalfEdges(start.elementId);
                            if (result.localEdgeIndex < static_cast<int>(faceHEs.size())) {
                                result.halfEdgeIdx = faceHEs[result.localEdgeIndex];
                            }

                            //std::cout << "[TraceInFace] Final bary edge hit: min_coord=" << minBary << " < " << BARY_SNAP_TOL << " t=" << t << "\n";
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
            //std::cerr << "[evaluateSurfacePoint] VERTEX: invalid index " << vIdx << "\n";
            return glm::dvec3(0.0);
        }
        return glm::dvec3(verts[vIdx].position);
    }
    case SurfacePoint::Type::EDGE: {
        uint32_t eIdx = point.elementId;
        if (eIdx >= edges.size()) {
            //std::cerr << "[evaluateSurfacePoint] EDGE: invalid edge index " << eIdx << "\n";
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
            //std::cerr << "[evaluateSurfacePoint] FACE: invalid face index " << fIdx << "\n";
            return glm::dvec3(0.0);
        }
        auto fv = conn.getFaceVertices(fIdx);
        if (fv.size() != 3) {
            //std::cerr << "[evaluateSurfacePoint] FACE: non-triangular face\n";
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
    //std::cerr << "[evaluateSurfacePoint] unknown SurfacePoint type\n";
    return glm::dvec3(0.0);
}

glm::dvec2 GeodesicTracer::chartLocal2D(uint32_t oldFaceIdx, uint32_t newFaceIdx, const glm::dvec2& oldPoint2D) const {
    // ***ONLY MAPS ADJACENT FACES***
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

glm::dvec2 GeodesicTracer::rotateVectorAcrossEdge(uint32_t oldFace, uint32_t oldHe, uint32_t newFace, uint32_t newHe, const glm::dvec2& vecInOld) const
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

    //std::cout << "[solveRayEdge] rayDir=(" << rayDir.x << "," << rayDir.y << ") edgeVec=(" << edgeVec.x << "," << edgeVec.y << ") b=(" << b.x << "," << b.y << ") det=" << det << std::endl;

    if (std::abs(det) < 1e-12) {
        //std::cout << "[solveRayEdge] parallel/degenerate, returning false" << std::endl;
        return false;
    }

    out_t = (b.y * edgeVec.x - b.x * edgeVec.y) / det;
    out_u = (rayDir.x * b.y - rayDir.y * b.x) / det;

    //std::cout << "[solveRayEdge] result: t=" << out_t << " u=" << out_u << std::endl;
    return true;
}

GeodesicTracer::GeodesicTraceResult GeodesicTracer::traceFromEdge(uint32_t startEdgeIdx, double startSplit, const glm::dvec2& cartesianDir, double length, uint32_t intrinsicHalfedgeIdx, uint32_t resolutionFace) const {
    /*
    std::cout << "[traceFromEdge] Starting edge trace from edge " << startEdgeIdx 
              << " split=" << startSplit
              << " dir=(" << cartesianDir.x << "," << cartesianDir.y << ")" 
              << " length=" << length 
              << " intrinsicHE=" << intrinsicHalfedgeIdx 
              << " resolutionFace=" << resolutionFace << std::endl;
    */

    GeodesicTraceResult result;
    result.success = false;

    const auto& conn = mesh.getConnectivity();
    const auto& edges = conn.getEdges();
    const auto& halfEdges = conn.getHalfEdges();

    // Get both halfedges of the edge
    uint32_t canonicalHe = edges[startEdgeIdx].halfEdgeIdx;
    if (canonicalHe == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[traceFromEdge] Edge has no canonical halfedge" << std::endl;
        return result;
    }

    uint32_t oppHe = halfEdges[canonicalHe].opposite;

    if (oppHe == HalfEdgeMesh::INVALID_INDEX) {
        //std::cout << "[traceFromEdge] Boundary edge" << std::endl;
        result.success = false;
        SurfacePoint edgePoint;
        edgePoint.type = SurfacePoint::Type::EDGE;
        edgePoint.elementId = startEdgeIdx;
        edgePoint.split = startSplit;
        edgePoint.baryCoords = glm::dvec3(1.0 - startSplit, startSplit, 0.0);
        result.exitPoint = edgePoint;
        result.position3D = glm::vec3(evaluateSurfacePoint(edgePoint));
        return result;
    }

    // Get the two adjacent faces
    uint32_t face1 = halfEdges[canonicalHe].face;
    uint32_t face2 = halfEdges[oppHe].face;

    //std::cout << "[traceFromEdge] Edge halfedges: canonical=" << canonicalHe << " (face=" << face1 << "), opp=" << oppHe << " (face=" << face2 << ")" << std::endl;

    const auto& hvf = mesh.getHalfedgeVectorsInFace();
    
    uint32_t sourceFace = resolutionFace;  // The face the direction is expressed in
    uint32_t traceHe, targetFace;
    double tEdge;
    glm::dvec2 dirInTargetFace;
    
    // If no resolution face, default to canonical face
    if (sourceFace == HalfEdgeMesh::INVALID_INDEX) {
        sourceFace = face1;
        std::cout << "[traceFromEdge] No resolution face provided, defaulting to face1=" << sourceFace << std::endl;
    }
    
    // Find the face to trace into based on which direction points into which face
    if (sourceFace == face1) {
        // Direction is in face1's chart, now check if it points into face1 or face2
        glm::dvec2 normal1(-hvf[canonicalHe].y, hvf[canonicalHe].x);
        double projIntoFace1 = glm::dot(cartesianDir, normal1);
        
        if (projIntoFace1 > 0) {
            // Points into face1
            traceHe = canonicalHe;
            targetFace = face1;
            tEdge = startSplit;
            dirInTargetFace = cartesianDir;
        } else {
            // Points into face2, now rotate from face1 to face2
            traceHe = oppHe;
            targetFace = face2;
            tEdge = 1.0 - startSplit;
            dirInTargetFace = rotateVectorAcrossEdge(face1, canonicalHe, face2, oppHe, cartesianDir);
        }
    } else {  
        // Direction is in face2's chart, now check if it points into face2 or face1
        glm::dvec2 normal2(-hvf[oppHe].y, hvf[oppHe].x);
        double projIntoFace2 = glm::dot(cartesianDir, normal2);
        
        if (projIntoFace2 > 0) {
            // Points into face2
            traceHe = oppHe;
            targetFace = face2;
            tEdge = 1.0 - startSplit;
            dirInTargetFace = cartesianDir;
        } else {
            // Points into face1, now rotate from face2 to face1
            traceHe = canonicalHe;
            targetFace = face1;
            tEdge = startSplit;
            dirInTargetFace = rotateVectorAcrossEdge(face2, oppHe, face1, canonicalHe, cartesianDir);
        }
    }
    
    //std::cout << "[traceFromEdge] Final: targetFace=" << targetFace << " dir=(" << dirInTargetFace.x << "," << dirInTargetFace.y << ")" << std::endl;

    // Set starting barycentric coordinates on the edge in the target face
    auto faceHEs = conn.getFaceHalfEdges(targetFace);
    int edgeOrigin = -1;
    for (int i = 0; i < 3; ++i) {
        if (faceHEs[i] == traceHe) {
            edgeOrigin = i;
            break;
        }
    }
    
    if (edgeOrigin == -1) {
        //std::cout << "[traceFromEdge] Could not find halfedge " << traceHe << " in face " << targetFace << std::endl;
        return result;
    }

    // Convert edge split to barycentric coordinates
    // tEdge represents how far along the halfedge from origin to target
    int edgeTarget = (edgeOrigin + 1) % 3;
    glm::dvec3 startPoint(0.0);
    startPoint[edgeOrigin] = 1.0 - tEdge;       // Origin vertex gets (1-t)
    startPoint[edgeTarget] = tEdge;             // Target vertex gets t

    //std::cout << "[traceFromEdge] Final: face=" << targetFace << " startBary=(" << startPoint.x << "," << startPoint.y << "," << startPoint.z << ") dir=(" << dirInTargetFace.x << "," << dirInTargetFace.y << ")" << std::endl;

    // Send to face tracer
    return traceFromFace(targetFace, startPoint, dirInTargetFace, length);
}
