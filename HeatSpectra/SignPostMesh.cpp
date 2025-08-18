#include <glm/gtc/constants.hpp>

#include <iostream>
#include <algorithm>
#include <queue>

#include "Model.hpp"
#include "SignPostMesh.hpp"

void SignpostMesh::buildFromModel(const Model& src) {
    // 1) build connectivity + vertex positions
    conn.buildFromModel(src);
    auto& HEs = conn.getHalfEdges();
    auto& V = conn.getVertices();

    // 2) Build faceNormals 
    const auto& connFaces = conn.getFaces();
    faceNormals.resize(connFaces.size());
    for (uint32_t fid = 0; fid < connFaces.size(); ++fid) {
        uint32_t startHe = connFaces[fid].halfEdgeIdx;
        if (startHe == INVALID_INDEX) {
            faceNormals[fid] = glm::vec3(0.0f);
            continue;
        }

        // walk the three halfedges of this face
        uint32_t he1 = startHe;
        uint32_t he2 = HEs[he1].next;
        uint32_t he3 = HEs[he2].next;

        // get their origin vertex indices
        uint32_t v0 = HEs[he1].origin;
        uint32_t v1 = HEs[he2].origin;
        uint32_t v2 = HEs[he3].origin;

        // sanity check 
        if (v0 >= V.size() ||
            v1 >= V.size() ||
            v2 >= V.size()) {
            faceNormals[fid] = glm::vec3(0.0f);
            continue;
        }

        // compute triangle normal
        glm::vec3 A = V[v0].position;
        glm::vec3 B = V[v1].position;
        glm::vec3 C = V[v2].position;
        faceNormals[fid] = glm::normalize(glm::cross(B - A, C - A));
    }

    // 3) set Euclidean lengths directly in HalfEdge struct
    for (uint32_t he = 0; he < HEs.size(); ++he) {
        uint32_t v1 = HEs[he].origin;
        uint32_t heN = HEs[he].next;

        // Check if next is valid
        if (heN == INVALID_INDEX || v1 == INVALID_INDEX) continue;

        uint32_t v2 = HEs[heN].origin;

        // Check if indices are in range
        if (v1 >= V.size() || v2 >= V.size()) continue;

        // Set intrinsic length
        glm::dvec3 dv1(V[v1].position.x, V[v1].position.y, V[v1].position.z);
        glm::dvec3 dv2(V[v2].position.x, V[v2].position.y, V[v2].position.z);
        HEs[he].intrinsicLength = glm::length(dv2 - dv1);
    }
}

void SignpostMesh::applyToModel(Model& dstModel) const {
    // 1) Rebuild vertex array
    std::vector<::Vertex> newVtx;
    const auto& HEM_V = conn.getVertices();
    newVtx.reserve(HEM_V.size());

    for (uint32_t i = 0; i < HEM_V.size(); ++i) {
        ::Vertex v;
        // Use HEM_V[i].position instead of vertexPositions[i]
        v.pos = HEM_V[i].position;

        if (HEM_V[i].originalIndex < dstModel.getVertexCount()) {
            const auto& o = dstModel.getVertices()[HEM_V[i].originalIndex];
            v.color = o.color;
            v.normal = o.normal;
            v.texCoord = o.texCoord;
        }
        else {
            v.color = glm::vec3(0.0f);
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.texCoord = glm::vec2(0.0f);
        }

        newVtx.push_back(v);
    }

    // 2) Build index buffer from faces (still needed for triangle rendering)
    std::vector<uint32_t> newIdx;
    const auto& HEs = conn.getHalfEdges();
    const auto& fdata = conn.getFaces();

    int orphanedFaces = 0;
    for (uint32_t fid = 0; fid < fdata.size(); ++fid) {
        uint32_t h0 = fdata[fid].halfEdgeIdx;
        if (h0 == INVALID_INDEX) {
            ++orphanedFaces;
            continue;
        }

        uint32_t h1 = HEs[h0].next;
        uint32_t h2 = HEs[h1].next;
        if (HEs[h2].next != h0) {
#ifndef NDEBUG
            std::cerr << "Warning: face " << fid << " is not a triangle!\n";
#endif
            continue;
        }

        uint32_t v0 = HEs[h0].origin;
        uint32_t v1 = HEs[h1].origin;
        uint32_t v2 = HEs[h2].origin;

        newIdx.push_back(v0);
        newIdx.push_back(v1);
        newIdx.push_back(v2);
    }

#ifndef NDEBUG
    std::cout << "Exporting mesh with " << fdata.size() << " faces\n";
    std::cout << "Orphaned faces: " << orphanedFaces << "\n";
#endif

    // 3) Edge diagnostics using ONLY conn.getEdges()
    const auto& edgeList = conn.getEdges();
    std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> edgeSet;
    int duplicateEdgeCount = 0;

    for (const auto& edge : edgeList) {
        uint32_t he = edge.halfEdgeIdx;
        if (he == INVALID_INDEX) continue;

        uint32_t v1 = HEs[he].origin;
        uint32_t v2 = INVALID_INDEX;
        if (HEs[he].next != INVALID_INDEX)
            v2 = HEs[HEs[he].next].origin;

        if (v1 == INVALID_INDEX || v2 == INVALID_INDEX) continue;

        auto e = std::minmax(v1, v2);
        if (!edgeSet.insert(e).second) {
#ifndef NDEBUG
            std::cerr << "WARNING: Duplicate edge " << v1 << "-" << v2 << " in edge list\n";
#endif
            ++duplicateEdgeCount;
        }
    }

#ifndef NDEBUG
    std::cout << "Unique edges in original edge list: " << edgeSet.size()
        << " (from " << edgeList.size() << " edge entries)\n";
    std::cout << "Detected " << duplicateEdgeCount << " duplicate edges in original edge list\n";
#endif

    // 4) Final upload
    dstModel.setVertices(newVtx);
    dstModel.setIndices(newIdx);
    dstModel.recalculateNormals();

#ifndef NDEBUG
    std::cout << "Final model: " << newVtx.size() << " vertices, "
        << newIdx.size() / 3 << " faces\n";
#endif
}

void SignpostMesh::initializeIntrinsicGeometry() {
    auto& HEs = conn.getHalfEdges();
    const auto& edges = conn.getEdges();
    const auto& vertices = conn.getVertices();

    // 1) Calculate Euclidean lengths for all halfedges
    for (uint32_t edgeIdx = 0; edgeIdx < edges.size(); edgeIdx++) {
        uint32_t he = edges[edgeIdx].halfEdgeIdx;
        if (he == INVALID_INDEX) continue;

        // Get the vertices of this edge
        uint32_t v1 = HEs[he].origin;
        uint32_t v2;
        uint32_t opp = HEs[he].opposite;

        if (opp != INVALID_INDEX) {
            // For interior edges, get the origin of the opposite halfedge
            v2 = HEs[opp].origin;
        }
        else {
            // For boundary edges, get the next vertex in the face
            uint32_t nextHe = HEs[he].next;
            if (nextHe == INVALID_INDEX) continue;
            v2 = HEs[nextHe].origin;
        }

        if (v1 >= vertices.size() || v2 >= vertices.size()) continue;

        // Calculate the Euclidean length
        glm::dvec3 dv1(vertices[v1].position.x, vertices[v1].position.y, vertices[v1].position.z);
        glm::dvec3 dv2(vertices[v2].position.x, vertices[v2].position.y, vertices[v2].position.z);
        double length = glm::length(dv2 - dv1);

        // Ensure minimum positive length
        length = std::max(length, 1e-12);

        // Store the length directly in the HalfEdge struct
        HEs[he].intrinsicLength = length;

        // Also set for the opposite halfedge if it exists
        if (opp != INVALID_INDEX) {
            HEs[opp].intrinsicLength = length;
        }
    }

    // 2) Calculate corner angles for all faces
    const auto& faces = conn.getFaces();
    for (uint32_t faceIdx = 0; faceIdx < faces.size(); faceIdx++) {
        updateCornerAnglesForFace(faceIdx);
    }

    // 3) Initialize signpost angles by walking around each vertex
    const auto& V = conn.getVertices();
    for (uint32_t vid = 0; vid < V.size(); vid++) {
        uint32_t startHe = V[vid].halfEdgeIdx;
        if (startHe == INVALID_INDEX) continue;

        uint32_t curr = startHe;
        double running = 0.0;

        do {
            HEs[curr].signpostAngle = running;
            running += HEs[curr].cornerAngle;

            // Move to the next halfedge around this vertex
            uint32_t nextHe = conn.getNextAroundVertex(curr);
            if (nextHe == INVALID_INDEX) break;

            curr = nextHe;
        } while (curr != startHe && curr != INVALID_INDEX);
    }

    // Print statistics to verify
    printMeshStatistics();
}

SignpostMesh::Triangle2D SignpostMesh::layoutTriangle(uint32_t faceIdx) const {
    Triangle2D result;

    auto faceHEs = conn.getFaceHalfEdges(faceIdx);
    if (faceHEs.size() != 3) {
        // Return degenerate triangle
        for (int i = 0; i < 3; i++) {
            result.vertices[i] = glm::dvec2(0.0);
            result.indices[i] = INVALID_INDEX;
            result.edgeLengths[i] = 0.0;
        }
        return result;
    }

    // vertex indices (same as before)
    const auto& HEs = conn.getHalfEdges();
    for (int i = 0; i < 3; ++i) result.indices[i] = HEs[faceHEs[i]].origin;

    // edge lengths (intrinsic)
    result.edgeLengths[0] = HEs[faceHEs[0]].intrinsicLength; // edge 0->1
    result.edgeLengths[1] = HEs[faceHEs[1]].intrinsicLength; // edge 1->2
    result.edgeLengths[2] = HEs[faceHEs[2]].intrinsicLength; // edge 2->0

    // If halfedgeVectorsInFace is available and sized correctly, use it to build chart
    if (halfedgeVectorsInFace.size() == conn.getHalfEdges().size()) {
        const double MIN_LEN = 1e-12;
        glm::dvec2 v01 = halfedgeVectorsInFace[faceHEs[0]]; // from v0 -> v1
        glm::dvec2 v21 = halfedgeVectorsInFace[faceHEs[2]]; // from v2 -> v0 (note orientation)
        if (glm::length(v01) > MIN_LEN && glm::length(v21) > MIN_LEN) {
            result.vertices[0] = glm::dvec2(0.0, 0.0);
            result.vertices[1] = v01;                // place v1 at halfedge vector
            result.vertices[2] = -v21;               // GC convention: v2 = -halfedgeVec(previous)
            return result;
        }      
    }

    // Fallback: law of cosines canonical layout (positive y)
    double a = result.edgeLengths[0];
    double b = result.edgeLengths[1];
    double c = result.edgeLengths[2];

    // validate
    const double MIN_LENGTH = 1e-12;
    if (a < MIN_LENGTH || b < MIN_LENGTH || c < MIN_LENGTH) {
        std::cout << "[layoutTriangle] Edge length: FAILED for face:" << faceIdx
            << "(a=" << a << ", b=" << b << ", c=" << c << ")" << std::endl;
        return result;
    }
    const double EPS = 1e-12;
    if (!(a + b > c + EPS && a + c > b + EPS && b + c > a + EPS)) {
        std::cout << "[layoutTriangle] Triangle inequality: FAILED for face:" << faceIdx
            << "(a=" << a << ", b=" << b << ", c=" << c << ")" << std::endl;
        return result;
    }

    result.vertices[0] = glm::dvec2(0.0, 0.0);
    result.vertices[1] = glm::dvec2(a, 0.0);

    double x = (a * a + c * c - b * b) / (2.0 * a);
    double y2 = c * c - x * x;
    double y = (y2 > 0.0) ? std::sqrt(y2) : 0.0; // canonical +y fallback
    result.vertices[2] = glm::dvec2(x, y);

    return result;
}

void SignpostMesh::updateSignpostAngles(uint32_t he) {
    auto& HEs = conn.getHalfEdges();

    // prev halfedge in this face is HEs[he].prev
    uint32_t prevHe = HEs[he].prev;

    // Get the base angle from the previous half-edge's signpost angle
    double base = HEs[prevHe].signpostAngle;
    double corner = HEs[prevHe].cornerAngle;
    double ang = base + corner;

    // wrap into [0,2pi)
    ang = std::fmod(ang, 2 * glm::pi<double>());
    if (ang < 0) ang += 2 * glm::pi<double>();

    // Store the signpost angle directly in the half-edge
    HEs[he].signpostAngle = ang;
}

void SignpostMesh::updateAllSignposts() {
    auto& HEs = conn.getHalfEdges();
    auto& V = conn.getVertices();

    for (uint32_t vid = 0; vid < V.size(); ++vid) {
        auto hes = conn.getVertexHalfEdges(vid);
        if (hes.empty()) continue;

        // Set reference direction 
        HEs[hes[0]].signpostAngle = 0.0;

        // Accumulate the *previous* cornerAngle into each next halfedge
        double acc = 0.0;
        for (size_t i = 1; i < hes.size(); ++i) {
            acc += HEs[hes[i - 1]].cornerAngle;
            HEs[hes[i]].signpostAngle = acc;
        }
    }
}

void SignpostMesh::buildHalfedgeVectorsInVertex() {
    auto& halfEdges = conn.getHalfEdges();
    auto& vertices = conn.getVertices();

    halfedgeVectorsInVertex.clear();
    halfedgeVectorsInVertex.resize(halfEdges.size(), glm::dvec2(0.0));

    for (uint32_t v = 0; v < vertices.size(); ++v) {
        if (vertices[v].halfEdgeIdx == INVALID_INDEX) continue;

        // Get all halfedges around this vertex
        auto vertexHEs = conn.getVertexHalfEdges(v);
        if (vertexHEs.empty()) continue;

        for (uint32_t he : vertexHEs) {
            if (he >= halfEdges.size()) continue;

            // The halfedge vector should point in the direction of its signpost angle
            double signpostAngle = halfEdges[he].signpostAngle;

            // Create unit vector in the true geometric direction
            halfedgeVectorsInVertex[he] = glm::dvec2(
                std::cos(signpostAngle),
                std::sin(signpostAngle)
            );

            // Verify unit magnitude
            double magnitude = glm::length(halfedgeVectorsInVertex[he]);
            if (std::abs(magnitude - 1.0) > 1e-10) {
#ifndef NDEBUG
                std::cout << "[buildHalfedgeVectors] WARNING: Not a unit vector he=" << he
                    << " magnitude=" << magnitude << std::endl;
#endif
            }
        }
    }
}

void SignpostMesh::buildHalfedgeVectorsInFace() {
    const auto& faces = conn.getFaces();
    const auto& halfEdges = conn.getHalfEdges();

    halfedgeVectorsInFace.clear();
    halfedgeVectorsInFace.resize(halfEdges.size(), glm::dvec2(0.0));

    for (uint32_t f = 0; f < faces.size(); ++f) {
        auto fh = conn.getFaceHalfEdges(f);
        if (fh.size() != 3) continue;

        // Lay out face in canonical coordinates
        Triangle2D tri = layoutTriangle(f);

        for (int k = 0; k < 3; ++k) {
            uint32_t he = fh[k];
            glm::dvec2 from = tri.vertices[k];
            glm::dvec2 to = tri.vertices[(k + 1) % 3];
            halfedgeVectorsInFace[he] = to - from;
        }
    }
}

glm::dvec2 SignpostMesh::computeCircumcenter2D(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const {
    double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));

    if (std::abs(d) < 1e-12) {
        return glm::dvec2(std::numeric_limits<double>::quiet_NaN());
    }

    double aSq = a.x * a.x + a.y * a.y;
    double bSq = b.x * b.x + b.y * b.y;
    double cSq = c.x * c.x + c.y * c.y;

    double x = (aSq * (b.y - c.y) + bSq * (c.y - a.y) + cSq * (a.y - b.y)) / d;
    double y = (aSq * (c.x - b.x) + bSq * (a.x - c.x) + cSq * (b.x - a.x)) / d;

    return glm::dvec2(x, y);
}

glm::dvec3 SignpostMesh::computeBarycentric2D(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const {
    glm::dvec2 v0 = c - a;
    glm::dvec2 v1 = b - a;
    glm::dvec2 v2 = p - a;

    double dot00 = glm::dot(v0, v0);
    double dot01 = glm::dot(v0, v1);
    double dot02 = glm::dot(v0, v2);
    double dot11 = glm::dot(v1, v1);
    double dot12 = glm::dot(v1, v2);

    double invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01);
    double u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    double v = (dot00 * dot12 - dot01 * dot02) * invDenom;
    double w = 1.0 - u - v;
    return glm::dvec3(w, v, u);
}

double SignpostMesh::computeSplitDiagonalLength(uint32_t faceIdx, uint32_t originalVA, uint32_t originalVB, double splitFraction) const {
    // 1) Lay out the triangle in 2D using intrinsic edge lengths
    auto triangle2D = layoutTriangle(faceIdx);
    auto faceVertices = conn.getFaceVertices(faceIdx);  // [v0, v1, v2] in CCW order

#ifndef NDEBUG
    std::cout << "[computeSplitDiagonalLength] Face " << faceIdx
        << " vertices: [" << faceVertices[0] << ", " << faceVertices[1] << ", " << faceVertices[2] << "]"
        << " looking for edge " << originalVA << "->" << originalVB << std::endl;
#endif

    if (faceVertices.size() != 3) {
#ifndef NDEBUG
        std::cout << "[computeSplitDiagonalLength] Not a triangle, returning 0" << std::endl;
#endif
        return 0.0;
    }

    // 2) Find which indices belong to the original edge's endpoints
    int idxA = -1, idxB = -1, idxC = -1;
    for (int i = 0; i < 3; ++i) {
        if (faceVertices[i] == originalVA)       
            idxA = i;
        else if (faceVertices[i] == originalVB)  
            idxB = i;
        else                           
            idxC = i;
    }

    if (idxA < 0 || idxB < 0 || idxC < 0) {
#ifndef NDEBUG
        std::cout << "[computeSplitDiagonalLength] Vertex not found in face: idxA=" << idxA
            << " idxB=" << idxB << " idxC=" << idxC << " - returning 0" << std::endl;
#endif
        return 0.0;
    }

    // 3) Compute split point P = (1�t)*A + t*B in that 2D triangle
    glm::dvec2 A = triangle2D.vertices[idxA];
    glm::dvec2 B = triangle2D.vertices[idxB];
    glm::dvec2 P = (1.0 - splitFraction) * A + splitFraction * B;

    // 4) Distance from P to the third corner
    glm::dvec2 C = triangle2D.vertices[idxC];
    double result = glm::length(P - C);

    // DEBUG
#ifndef NDEBUG
    std::cout << "[computeSplitDiagonalLength] 2D triangle:" << std::endl;
    std::cout << "  A=" << triangle2D.vertices[idxA].x << "," << triangle2D.vertices[idxA].y << std::endl;
    std::cout << "  B=" << triangle2D.vertices[idxB].x << "," << triangle2D.vertices[idxB].y << std::endl;
    std::cout << "  C=" << triangle2D.vertices[idxC].x << "," << triangle2D.vertices[idxC].y << std::endl;
    std::cout << "  Split point P=" << P.x << "," << P.y << std::endl;
    std::cout << "  Distance P->C=" << result << std::endl;
#endif

#ifndef NDEBUG
    std::cout << "[computeSplitDiagonalLength] SUCCESS: diagonal length = " << result << std::endl;
#endif
    return result;
}

double SignpostMesh::computeAngleFromLengths(double a, double b, double c, uint32_t faceIdx) const {
    // Ensure minimum positive edge length for numerical stability
    const float MIN_LENGTH = 1e-6f;

    // Check if any edge is too small or triangle inequality is violated
    if (a < MIN_LENGTH || b < MIN_LENGTH || c < MIN_LENGTH ||
        a + b < c || a + c < b || b + c < a) {
#ifndef NDEBUG
        std::cout << "[computeAngleFromLengths] Degenerate triangle detected in face " << faceIdx << "\n"
            << " (a=" << a << " b=" << b << " c=" << c << ") - Skipping angle computation!" << std::endl;
#endif
        // Return a degenerate triangle value
        return -1.0f;
    }

    // Compute the angle using the law of cosines
    double cosAngle = (b * b + c * c - a * a) / (2.0 * b * c);

    // Clamp to valid range to avoid numerical issues
    cosAngle = std::max(-1.0, std::min(1.0, cosAngle));

    return std::acos(cosAngle);
}

void SignpostMesh::updateCornerAnglesForFace(uint32_t faceIdx) {
    const auto& faces = conn.getFaces();
    auto& HEs = conn.getHalfEdges();

    if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX)
        return;

    // Get the three halfedges of this face
    uint32_t he0 = faces[faceIdx].halfEdgeIdx;
    uint32_t he1 = HEs[he0].next;
    uint32_t he2 = HEs[he1].next;

    if (he0 == INVALID_INDEX || he1 == INVALID_INDEX || he2 == INVALID_INDEX)
        return;

    // Get the three edge lengths
    double a = HEs[he0].intrinsicLength;
    double b = HEs[he1].intrinsicLength;
    double c = HEs[he2].intrinsicLength;

    // Calculate using the law of cosines
    double angleAtV0 = computeAngleFromLengths(b, c, a, faceIdx);  // Angle at origin of he0
    double angleAtV1 = computeAngleFromLengths(c, a, b, faceIdx);  // Angle at origin of he1
    double angleAtV2 = computeAngleFromLengths(a, b, c, faceIdx);  // Angle at origin of he2

    // Each halfedge gets the angle at its origin vertex
    HEs[he0].cornerAngle = angleAtV0;  // Angle at origin of he0
    HEs[he1].cornerAngle = angleAtV1;  // Angle at origin of he1
    HEs[he2].cornerAngle = angleAtV2;  // Angle at origin of he2
}

void SignpostMesh::updateAllCornerAngles(const std::unordered_set<uint32_t>& skipFaces) {
    const auto& faces = conn.getFaces();
    for (uint32_t f = 0; f < faces.size(); ++f) {
        if (skipFaces.count(f)) 
            continue;           
        updateCornerAnglesForFace(f);
    }
}

float SignpostMesh::computeFaceArea(uint32_t faceIdx) const {
    const auto& faces = conn.getFaces();
    const auto& halfEdges = conn.getHalfEdges();

    if (faceIdx >= faces.size()) {
        return 0.0f;
    }

    // Get the half-edges of this face
    std::vector<uint32_t> faceEdges = conn.getFaceHalfEdges(faceIdx);
    if (faceEdges.size() != 3) {
        return 0.0f;
    }

    // Get the edge lengths directly from the half-edges
    double a = halfEdges[faceEdges[0]].intrinsicLength;
    double b = halfEdges[faceEdges[1]].intrinsicLength;
    double c = halfEdges[faceEdges[2]].intrinsicLength;

    // Ensure minimum positive length for numerical stability
    a = std::max(a, 1e-12);
    b = std::max(b, 1e-12);
    c = std::max(c, 1e-12);

    // Compute semi-perimeter
    double s = (a + b + c) / 2.0f;

    // Compute area using Heron's formula
    double area = std::sqrt(std::max(0.0, s * (s - a) * (s - b) * (s - c)));

    return static_cast<float>(area);
}

std::vector<float> SignpostMesh::getAllFaceAreas() const {
    std::vector<float> areas;
    areas.reserve(conn.getFaces().size());

    for (uint32_t i = 0; i < conn.getFaces().size(); ++i) {
        areas.push_back(computeFaceArea(i));
    }

    return areas;
}

bool SignpostMesh::isEdgeOnBoundary(uint32_t heIdx) const {
    const auto& HEs = conn.getHalfEdges();
    return (heIdx < HEs.size() && HEs[heIdx].opposite == INVALID_INDEX);
}

bool SignpostMesh::isBoundaryVertex(uint32_t vertexIdx) const {
    return conn.isBoundaryVertex(vertexIdx);
}

std::vector<uint32_t> SignpostMesh::getBoundaryVertices() const {
    std::vector<uint32_t> boundaryVerts;

    for (uint32_t i = 0; i < conn.getVertices().size(); ++i) {
        if (conn.isBoundaryVertex(i)) {
            boundaryVerts.push_back(i);
        }
    }

    return boundaryVerts;
}

uint32_t SignpostMesh::getVertexDegree(uint32_t vertexIdx) const {
    if (vertexIdx >= conn.getVertices().size()) {
        return 0;
    }

    std::vector<uint32_t> vertexHEs = conn.getVertexHalfEdges(vertexIdx);
    return static_cast<uint32_t>(vertexHEs.size());
}

double SignpostMesh::getCornerAngle(uint32_t halfEdgeIdx) const {
    const auto& HEs = conn.getHalfEdges();
    return (halfEdgeIdx < HEs.size())
        ? HEs[halfEdgeIdx].cornerAngle
        : 0.0;
}

void SignpostMesh::printMeshStatistics() const {
    const auto& edges = conn.getEdges();
    const auto& faces = conn.getFaces();
    const auto& vertices = conn.getVertices();
    const auto& halfEdges = conn.getHalfEdges();

    double minLength = FLT_MAX;
    double maxLength = 0.0f;
    double totalLength = 0.0f;
    int validEdgeCount = 0;

    // Compute edge length statistics from halfEdge class
    for (uint32_t i = 0; i < halfEdges.size(); i++) {
        double length = halfEdges[i].intrinsicLength;
        if (length > 0.0f) {
            minLength = std::min(minLength, length);
            maxLength = std::max(maxLength, length);
            totalLength += length;
            validEdgeCount++;
        }
    }

    double avgLength = validEdgeCount > 0 ? totalLength / validEdgeCount : 0.0f;

#ifndef NDEBUG
    std::cout << "Mesh Statistics:" << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Faces: " << faces.size() << std::endl;
    std::cout << "  Edges: " << edges.size() << std::endl;
    std::cout << "  Half-edges with valid length: " << validEdgeCount << std::endl;
    std::cout << "  Edge lengths - Min: " << minLength << ", Max: " << maxLength
        << ", Avg: " << avgLength << std::endl;
#endif
}