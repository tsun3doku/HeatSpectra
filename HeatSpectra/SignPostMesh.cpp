#include <glm/gtc/constants.hpp>

#include <iostream>
#include <algorithm>
#include <queue>
#include <cstdlib>

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

    // 3) set Euclidean lengths in Edge objects
    auto& edges = conn.getEdges();
    for (uint32_t edgeIdx = 0; edgeIdx < edges.size(); ++edgeIdx) {
        uint32_t he = edges[edgeIdx].halfEdgeIdx;
        if (he == INVALID_INDEX || he >= HEs.size()) continue;
        
        uint32_t v1 = HEs[he].origin;
        uint32_t heN = HEs[he].next;

        // Check if next is valid
        if (heN == INVALID_INDEX || v1 == INVALID_INDEX)
            continue;

        uint32_t v2 = HEs[heN].origin;

        // Check if indices are in range
        if (v1 >= V.size() || v2 >= V.size())
            continue;

        // Set intrinsic length in edge object
        glm::dvec3 dv1(V[v1].position.x, V[v1].position.y, V[v1].position.z);
        glm::dvec3 dv2(V[v2].position.x, V[v2].position.y, V[v2].position.z);
        edges[edgeIdx].intrinsicLength = glm::length(dv2 - dv1);
    }
    
    // 4) Compute corner angles 
    updateAllCornerAngles(std::unordered_set<uint32_t>());
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
            //std::cerr << "Warning: face " << fid << " is not a triangle!\n";
            continue;
        }

        uint32_t v0 = HEs[h0].origin;
        uint32_t v1 = HEs[h1].origin;
        uint32_t v2 = HEs[h2].origin;

        newIdx.push_back(v0);
        newIdx.push_back(v1);
        newIdx.push_back(v2);
    }

    //std::cout << "Exporting mesh with " << fdata.size() << " faces\n";
    //std::cout << "Orphaned faces: " << orphanedFaces << "\n";

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
            //std::cerr << "WARNING: Duplicate edge " << v1 << "-" << v2 << " in edge list\n";
            ++duplicateEdgeCount;
        }
    }

    //std::cout << "Unique edges in original edge list: " << edgeSet.size() << " (from " << edgeList.size() << " edge entries)\n";
    //std::cout << "Detected " << duplicateEdgeCount << " duplicate edges in original edge list\n";

    // 4) Final upload
    dstModel.setVertices(newVtx);
    dstModel.setIndices(newIdx);
    dstModel.recalculateNormals();

    //std::cout << "Final model: " << newVtx.size() << " vertices, " << newIdx.size() / 3 << " faces\n";
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

    // vertex indices
    const auto& HEs = conn.getHalfEdges();
    for (int i = 0; i < 3; ++i) result.indices[i] = HEs[faceHEs[i]].origin;

    result.edgeLengths[0] = conn.getIntrinsicLengthFromHalfEdge(faceHEs[0]); // edge 0->1
    result.edgeLengths[1] = conn.getIntrinsicLengthFromHalfEdge(faceHEs[1]); // edge 1->2
    result.edgeLengths[2] = conn.getIntrinsicLengthFromHalfEdge(faceHEs[2]); // edge 2->0

    // Law of cosines canonical layout 
    double a = result.edgeLengths[0];
    double b = result.edgeLengths[1];
    double c = result.edgeLengths[2];

    // validate
    const double MIN_LENGTH = 1e-12;
    if (a < MIN_LENGTH || b < MIN_LENGTH || c < MIN_LENGTH) {
        //std::cout << "[layoutTriangle] Edge length: FAILED for face:" << faceIdx << "(a=" << a << ", b=" << b << ", c=" << c << ")" << std::endl;
        return result;
    }
    const double EPS = 1e-12;
    if (!(a + b > c + EPS && a + c > b + EPS && b + c > a + EPS)) {
        //std::cout << "[layoutTriangle] Triangle inequality: FAILED for face:" << faceIdx << "(a=" << a << ", b=" << b << ", c=" << c << ")" << std::endl;
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

void SignpostMesh::updateAllSignposts() {
    const auto& V = conn.getVertices();
    const auto& HEs = conn.getHalfEdges();
    vertexAngleSums.clear();
    vertexAngleSums.resize(V.size(), 0.0);

    // Calculate vertex angle sums by summing corner angles
    for (uint32_t heIdx = 0; heIdx < HEs.size(); ++heIdx) {
        uint32_t vertexIdx = HEs[heIdx].origin;
        if (vertexIdx < vertexAngleSums.size()) {
            vertexAngleSums[vertexIdx] += HEs[heIdx].cornerAngle;
        }
    }

    // Walk around vertices assigning angular directions 
    for (uint32_t vid = 0; vid < V.size(); ++vid) {
        uint32_t firstOutgoing = V[vid].halfEdgeIdx;  
        if (firstOutgoing == HalfEdgeMesh::INVALID_INDEX)
            continue;

        double runningAngle = 0.0;
        uint32_t currHe = firstOutgoing;
        int safety = 0;
        do {
            // Store signpost angle at origin vertex 
            conn.getHalfEdges()[currHe].signpostAngle = runningAngle;

            // Add corner angle for next iteration 
            double cornerAngleVal = conn.getHalfEdges()[currHe].cornerAngle;
            runningAngle += cornerAngleVal;

            // Break at boundary 
            if (conn.getHalfEdges()[currHe].opposite == HalfEdgeMesh::INVALID_INDEX) {
                break;
            }

            // Use standard traversal
            uint32_t next1 = conn.getHalfEdges()[currHe].next;
            if (next1 == HalfEdgeMesh::INVALID_INDEX)
                break;
            uint32_t next2 = conn.getHalfEdges()[next1].next;
            if (next2 == HalfEdgeMesh::INVALID_INDEX)
                break;
            uint32_t opp = conn.getHalfEdges()[next2].opposite;
            if (opp == HalfEdgeMesh::INVALID_INDEX)
                break;
            currHe = opp;
            
            if (++safety > 10000)
                break;
        } while (currHe != firstOutgoing);
    }
}

glm::dvec2 SignpostMesh::halfedgeVector(uint32_t heIdx) const {
    const auto& HEs = conn.getHalfEdges();
    if (heIdx >= HEs.size()) 
        return glm::dvec2(0.0);
    const auto& he = HEs[heIdx];

    // Get raw signpost angle and apply vertex angle scaling
    double rawAngle = he.signpostAngle;
    // Use origin vertex (where signpost angle is now stored)
    uint32_t originVertexIdx = he.origin;
    
    // Apply vertex angle scaling: scaleFac = target_angle_sum / intrinsic_angle_sum
    double scaleFac = 1.0;
    if (originVertexIdx < vertexAngleScales.size()) {
        scaleFac = vertexAngleScales[originVertexIdx];
    }
    
    double scaledAngle = rawAngle * scaleFac;
    double length = conn.getIntrinsicLengthFromHalfEdge(heIdx);
    glm::dvec2 result = glm::dvec2(std::cos(scaledAngle) * length, std::sin(scaledAngle) * length);

    // Get target vertex by following the halfedge
    uint32_t targetVertexIdx = HalfEdgeMesh::INVALID_INDEX;
    if (he.next != HalfEdgeMesh::INVALID_INDEX && he.next < HEs.size()) {
        targetVertexIdx = HEs[he.next].origin;
    }
    
    /*
    std::cout << "[halfedgeVector] he=" << heIdx << " (" << originVertexIdx << "->" << targetVertexIdx << ")"
              << " rawAngle=" << rawAngle << " scaleFac=" << scaleFac 
              << " scaledAngle=" << scaledAngle << " length=" << length 
              << " vector=(" << result.x << "," << result.y << ")\n";
    */

    return result;
}

bool SignpostMesh::isBoundaryVertex(uint32_t vertexIdx) const {
    if (vertexIdx >= conn.getVertices().size()) return false;
    
    uint32_t firstHE = conn.getVertices()[vertexIdx].halfEdgeIdx;
    if (firstHE == HalfEdgeMesh::INVALID_INDEX) return true;
    
    uint32_t currHE = firstHE;
    const auto& HEs = conn.getHalfEdges();
    
    do {
        if (HEs[currHE].opposite == HalfEdgeMesh::INVALID_INDEX) {
            return true; // Found boundary edge
        }
        
        // Traverse to next outgoing halfedge
        uint32_t next1 = HEs[currHE].next;
        if (next1 == HalfEdgeMesh::INVALID_INDEX) return true;
        uint32_t next2 = HEs[next1].next;
        if (next2 == HalfEdgeMesh::INVALID_INDEX) return true;
        currHE = HEs[next2].opposite;
        if (currHE == HalfEdgeMesh::INVALID_INDEX) return true;
        
    } while (currHE != firstHE);
    
    return false; // Complete loop without boundary
}

void SignpostMesh::buildHalfedgeVectorsInVertex() {
    auto& HEs = conn.getHalfEdges();
    const auto& V = conn.getVertices();
    
    halfedgeVectorsInVertex.clear();
    halfedgeVectorsInVertex.resize(HEs.size(), glm::dvec2(std::numeric_limits<double>::quiet_NaN()));

    for (uint32_t vid = 0; vid < V.size(); ++vid) {
        uint32_t firstOutgoing = V[vid].halfEdgeIdx;  
        if (firstOutgoing == HalfEdgeMesh::INVALID_INDEX) continue;
        /*
        // Only show debug for one vertex idx
        bool showDebug = (vid == 1);
        if (showDebug) {
            std::cout << "[buildHalfedgeVectorsInVertex] Processing vertex " << vid << " firstOutgoing=" << firstOutgoing << "\n";
        }
        */
        uint32_t currHe = firstOutgoing;
        int safety = 0;
        
        do {
            // Use stored signpost angle directly 
            double coordSum = conn.getHalfEdges()[currHe].signpostAngle;
            
            // Apply vertex angle scaling
            double scaleFactor = 1.0;
            if (vid < vertexAngleScales.size()) scaleFactor = vertexAngleScales[vid];
            coordSum *= scaleFactor;
            
            // Build vector using signpost angle and edge length 
            double length = conn.getIntrinsicLengthFromHalfEdge(currHe);
            halfedgeVectorsInVertex[currHe] = glm::dvec2(
                std::cos(coordSum) * length,
                std::sin(coordSum) * length
            );
            
            // Get scaled corner angle for debug output
            double scaledAngle = 0.0;
            if (currHe < cornerScaledAngles.size()) {
                scaledAngle = cornerScaledAngles[currHe]; 
            }
            /*
            if (showDebug) {
                // Compute origin and target robustly
                uint32_t origin = (currHe < HEs.size()) ? HEs[currHe].origin : HalfEdgeMesh::INVALID_INDEX;
                uint32_t target = HalfEdgeMesh::INVALID_INDEX;
                // preferred: next->origin is target
                if (HEs[currHe].next != HalfEdgeMesh::INVALID_INDEX && HEs[currHe].next < HEs.size()) {
                    target = HEs[HEs[currHe].next].origin;
                }
                else if (HEs[currHe].opposite != HalfEdgeMesh::INVALID_INDEX && HEs[currHe].opposite < HEs.size()) {
                    // fallback: opposite's origin is the other endpoint
                    target = HEs[HEs[currHe].opposite].origin;
                }

                double rawSignpostAngle = conn.getHalfEdges()[currHe].signpostAngle;
                std::cout << "  -> he=" << currHe << " (" << origin << "->" << target 
                         << ") rawSignpost=" << rawSignpostAngle << " scaledCoordSum=" << coordSum << " length=" << length 
                         << " vec=(" << halfedgeVectorsInVertex[currHe].x << "," << halfedgeVectorsInVertex[currHe].y 
                         << ") scaledAngle=" << scaledAngle << "\n";
            }
            */
            // Break at boundary 
            if (HEs[currHe].opposite == HalfEdgeMesh::INVALID_INDEX) {
                break;
            }
            
            // Use SAME traversal as updateAllSignposts: next->next->opposite 
            uint32_t next1 = HEs[currHe].next;
            if (next1 == HalfEdgeMesh::INVALID_INDEX)
                break;
            uint32_t next2 = HEs[next1].next;
            if (next2 == HalfEdgeMesh::INVALID_INDEX)
                break;
            uint32_t opp = HEs[next2].opposite;
            if (opp == HalfEdgeMesh::INVALID_INDEX)
                break;
            currHe = opp;
            
            if (++safety > 2000) break;
        } while (currHe != firstOutgoing);
        /*
        if (showDebug) {
            std::cout << "[buildHalfedgeVectorsInVertex] Finished vertex " << vid << std::endl;
        }
        */
    }
}

void SignpostMesh::buildHalfedgeVectorsInFace() {
    const auto& HEs = conn.getHalfEdges();
    halfedgeVectorsInFace.clear();
    halfedgeVectorsInFace.resize(HEs.size(), glm::dvec2(0.0));

    const uint32_t faceCount = conn.getFaces().size();
    for (uint32_t f = 0; f < faceCount; ++f) {
        auto faceHEs = conn.getFaceHalfEdges(f);
        if (faceHEs.size() != 3) continue;

        // Use the canonical triangle layout in this face
        Triangle2D tri = layoutTriangle(f);

        // faceHEs[i] has origin = tri.indices[i], and tri.vertices[i] is position of that origin
        for (int i = 0; i < 3; ++i) {
            int ni = (i + 1) % 3;
            glm::dvec2 edgeVec = tri.vertices[ni] - tri.vertices[i]; // V_next - V_curr
            halfedgeVectorsInFace[faceHEs[i]] = edgeVec;
        }
    }

    // Mark exterior / invalid halfedges 
    const auto& halfEdgesRef = conn.getHalfEdges();
    for (size_t i = 0; i < halfEdgesRef.size(); ++i) {
        if (halfEdgesRef[i].face == HalfEdgeMesh::INVALID_INDEX) {
            halfedgeVectorsInFace[i] = glm::dvec2(std::numeric_limits<double>::quiet_NaN());
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
    // Lay out the triangle in 2D using intrinsic edge lengths
    auto triangle2D = layoutTriangle(faceIdx);
    auto faceVertices = conn.getFaceVertices(faceIdx);  

    /*
    std::cout << "[computeSplitDiagonalLength] Face " << faceIdx
        << " vertices: [" << faceVertices[0] << ", " << faceVertices[1] << ", " << faceVertices[2] << "]"
        << " looking for edge " << originalVA << "->" << originalVB << std::endl;
    */

    if (faceVertices.size() != 3) {
        //std::cout << "[computeSplitDiagonalLength] Not a triangle, returning 0" << std::endl;
        return 0.0;
    }

    // Find which indices belong to the original edge's endpoints
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
        //std::cout << "[computeSplitDiagonalLength] Vertex not found in face: idxA=" << idxA << " idxB=" << idxB << " idxC=" << idxC << " - returning 0" << std::endl;
        return 0.0;
    }

    // Calculate split point P
    glm::dvec2 A = triangle2D.vertices[idxA];
    glm::dvec2 B = triangle2D.vertices[idxB];
    glm::dvec2 P = (1.0 - splitFraction) * A + splitFraction * B;

    // Distance from P to the third corner
    glm::dvec2 C = triangle2D.vertices[idxC];
    double result = glm::length(P - C);

    //std::cout << "[computeSplitDiagonalLength] SUCCESS: diagonal length = " << result << std::endl;
    return result;
}

double SignpostMesh::computeAngleFromLengths(double a, double b, double c, uint32_t faceIdx) const {
    // Ensure minimum positive edge length for numerical stability
    const float MIN_LENGTH = 1e-6f;

    // Check if any edge is too small or triangle inequality is violated
    if (a < MIN_LENGTH || b < MIN_LENGTH || c < MIN_LENGTH ||
        a + b < c || a + c < b || b + c < a) {
        return -1.0f;
    }

    // Compute the angle using the law of cosines
    double cosAngle = (b * b + c * c - a * a) / (2.0 * b * c);

    // Clamp to valid range to avoid numerical issues
    cosAngle = std::max(-1.0, std::min(1.0, cosAngle));

    return std::acos(cosAngle);
}

void SignpostMesh::computeCornerScaledAngles() {
    const auto& HEs = conn.getHalfEdges();
    const auto& V = conn.getVertices();

    cornerScaledAngles.clear();
    cornerScaledAngles.resize(HEs.size(), 0.0);

    // Need vertex angle sums available
    if (vertexAngleSums.size() != V.size()) {
        // Fallback: recompute vertex sums
        vertexAngleSums.clear();
        vertexAngleSums.resize(V.size(), 0.0);
        for (size_t he = 0; he < HEs.size(); ++he) {
            uint32_t vid = HEs[he].origin;
            if (vid < vertexAngleSums.size()) vertexAngleSums[vid] += HEs[he].cornerAngle;
        }
    }

    // For every halfedge, compute its scaled corner angle based on its corner's vertex
    for (size_t he = 0; he < HEs.size(); ++he) {
        uint32_t vid = HEs[he].origin;
        if (vid >= V.size() || vid >= vertexAngleSums.size()) {
            cornerScaledAngles[he] = HEs[he].cornerAngle; // fallback
            continue;
        }

        double vSum = vertexAngleSums[vid];
        double target = (HEs[he].opposite == HalfEdgeMesh::INVALID_INDEX) ? glm::pi<double>() : 2.0 * glm::pi<double>();
        if (vSum > 1e-12) {
            double scale = target / vSum;
            cornerScaledAngles[he] = HEs[he].cornerAngle * scale;
        }
        else {
            cornerScaledAngles[he] = HEs[he].cornerAngle;
        }
    }
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

    // Get the three edge lengths from edge objects
    double a = conn.getIntrinsicLengthFromHalfEdge(he0);
    double b = conn.getIntrinsicLengthFromHalfEdge(he1);
    double c = conn.getIntrinsicLengthFromHalfEdge(he2);

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

double SignpostMesh::standardizeAngleForVertex(uint32_t vertexIdx, double angleRad) const {
    // If boundary vertex, return angle unchanged
    if (isBoundaryVertex(vertexIdx)) {
        return angleRad;
    }
    double sum = getVertexAngleSum(vertexIdx); // your per-vertex angle sum (usually ~2π for interior)
    // wrap into [0, sum)
    double wrapped = std::fmod(angleRad, sum);
    if (wrapped < 0.0) wrapped += sum; 
    return wrapped;
}

void SignpostMesh::updateAngleFromCWNeighbor(uint32_t heIdx) {
    auto& HEs = conn.getHalfEdges();
    
    // Boundary handling 
    if (!conn.isInteriorHalfEdge(heIdx)) {
        // Last wedge angle
        HEs[heIdx].signpostAngle = getVertexAngleSum(HEs[heIdx].origin); 
        halfedgeVectorsInVertex[heIdx] = halfedgeVector(heIdx);
        return;
    }

    uint32_t twin = HEs[heIdx].opposite;
    if (!conn.isInteriorHalfEdge(twin)) {
        HEs[heIdx].signpostAngle = 0.0; // first wedge
        halfedgeVectorsInVertex[heIdx] = halfedgeVector(heIdx);
        return;
    }

    // CW neighbor = he.opp().next()
    uint32_t cw = HEs[twin].next; 
    // Neighbor stored raw angle
    double neighborRaw = HEs[cw].signpostAngle;
    // Corner angle of neighbor
    double cAngle = HEs[cw].cornerAngle;

    // Add neighbor raw angle and corner angle then convert into target vertex domain
    double updated = standardizeAngleForVertex(HEs[heIdx].origin, neighborRaw + cAngle);
    HEs[heIdx].signpostAngle = updated;
    halfedgeVectorsInVertex[heIdx] = halfedgeVector(heIdx);
}

float SignpostMesh::computeFaceArea(uint32_t faceIdx) const {
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

    // Ensure minimum positive length for numerical stability
    a = std::max(a, 1e-12);
    b = std::max(b, 1e-12);
    c = std::max(c, 1e-12);

    // Calculate semiperimeter
    double s = (a + b + c) / 2.0f;

    // Calculate area using Heron's formula
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

    // Compute edge length statistics from edge objects
    for (uint32_t i = 0; i < edges.size(); i++) {
        double length = edges[i].intrinsicLength;
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
    std::cout << "  Edges with valid length: " << validEdgeCount << std::endl;
    std::cout << "  Edge lengths - Min: " << minLength << ", Max: " << maxLength
        << ", Avg: " << avgLength << std::endl;
#endif
}

void SignpostMesh::computeVertexAngleScales() {
    const auto& V = conn.getVertices();   

    vertexAngleScales.clear();
    vertexAngleScales.resize(V.size(), 1.0);

    for (uint32_t vid = 0; vid < V.size(); ++vid) {
        double intrinsicSum = 0.0;
        if (vid < vertexAngleSums.size()) intrinsicSum = vertexAngleSums[vid];

        double target = conn.isBoundaryVertex(vid) ? glm::pi<double>() : 2.0 * glm::pi<double>();

        if (intrinsicSum > 1e-12) {
            vertexAngleScales[vid] = target / intrinsicSum;
        }
        else {
            vertexAngleScales[vid] = 1.0; 
        }
        /*
        // DEBUG:      
        std::cout << "[computeVertexAngleScales] v=" << vid
            << " intrinsicSum=" << intrinsicSum
            << " target=" << target
            << " scale=" << vertexAngleScales[vid] << "\n";  
            */
    }
}