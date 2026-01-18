#include "CommonSubdivision.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <cmath>
#include <iostream>

CommonSubdivision::CommonSubdivision(
    const SignpostMesh& intrinsicMesh,
    const SignpostMesh& inputMesh,
    const GeodesicTracer& tracer,
    const std::unordered_map<uint32_t, GeodesicTracer::SurfacePoint>& vertexLocations,
    TraceFn traceFn)
    : intrinsicMesh(intrinsicMesh)
    , inputMesh(inputMesh)
    , tracer(tracer)
    , vertexLocations(vertexLocations)
    , traceFn(traceFn)
{
}

void CommonSubdivision::build() {
    const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();
    

    // Build all intrinsic triangles
    intrinsicTriangles.clear();
    intrinsicTriangles.reserve(faces.size());
    
    for (uint32_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
        if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
            continue;
        }
        
        IntrinsicTriangle tri = buildIntrinsicTriangle(faceIdx);
        if (tri.faceIdx != INVALID_INDEX) {
            intrinsicTriangles.push_back(tri);
        }
    }
    
    std::cout << "[CommonSubdivision] Built " << intrinsicTriangles.size() << " intrinsic triangles" << std::endl;
    
    // Extract all 3D points from intrinsic triangles
    std::vector<glm::vec3> allPoints;
    std::vector<size_t> triangleStartIndices;
    
    for (auto& tri : intrinsicTriangles) {
        triangleStartIndices.push_back(allPoints.size());
        
        for (const auto& sp : tri.vertices) {
            glm::dvec3 pos3D = tracer.evaluateSurfacePoint(sp);
            allPoints.push_back(glm::vec3(pos3D));
        }
    }
    
    // Merge nearby points 
    const double MERGE_TOLERANCE = 1e-5;
    std::vector<uint32_t> pointMapping;
    std::vector<glm::vec3> mergedPoints = mergeNearbyPoints(allPoints, MERGE_TOLERANCE, pointMapping);
    
    // Convert merged points to Vertex format
    vertices.clear();
    vertices.resize(mergedPoints.size());
    for (size_t i = 0; i < mergedPoints.size(); ++i) {
        vertices[i].pos = mergedPoints[i];
        vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
        vertices[i].color = glm::vec3(1.0f);
        vertices[i].texCoord = glm::vec2(0.0f);
    }
    
    // Store deduplicated indices and identify corners
    for (size_t triIdx = 0; triIdx < intrinsicTriangles.size(); ++triIdx) {
        size_t startIdx = triangleStartIndices[triIdx];
        auto& tri = intrinsicTriangles[triIdx];
        
        tri.indices.clear();
        tri.indices.reserve(tri.vertices.size());
        
        for (size_t i = 0; i < tri.vertices.size(); ++i) {
            uint32_t mergedIdx = pointMapping[startIdx + i];
            tri.indices.push_back(mergedIdx);
        }
        
        // Find corner indices using barycentric coordinates
        size_t cornerCount = 0;
        std::array<bool, 3> cornerFound = {false, false, false};
        
        for (size_t i = 0; i < tri.vertices.size() && cornerCount < 3; ++i) {
            const auto& bary = tri.baryCoords[i];
            
            for (size_t c = 0; c < 3; ++c) {
                if (cornerFound[c]) 
                    continue;
                
                // Check if this is corner
                bool isThisCorner = (bary[c] > 0.99) && 
                                   (bary[(c+1)%3] < 0.01) && 
                                   (bary[(c+2)%3] < 0.01);
                
                if (isThisCorner) {
                    tri.cornerIndices[c] = i;
                    cornerFound[c] = true;
                    cornerCount++;
                    break;
                }
            }
        }
    }
    
    size_t duplicatesMerged = allPoints.size() - vertices.size();
    std::cout << "[CommonSubdivision] Merged vertices: " << allPoints.size() << " → " << vertices.size()
              << " (merged " << duplicatesMerged << " duplicates)" << std::endl;
    
    // Generate colors for visualization
    std::vector<glm::vec3> faceColors = generateFaceColors(intrinsicTriangles.size());
    
    // Build triangles by grouping points by input face
    indices.clear();
    auto& inputConn = inputMesh.getConnectivity();
    
    for (size_t triIdx = 0; triIdx < intrinsicTriangles.size(); ++triIdx) {
        glm::vec3 color = faceColors[triIdx];
        auto& tri = intrinsicTriangles[triIdx];
        
        // Group points by input face
        std::map<uint32_t, std::vector<size_t>> pointsByInputFace;
        
        for (size_t i = 0; i < tri.vertices.size(); ++i) {
            const auto& sp = tri.vertices[i];
            std::vector<uint32_t> adjacentFaces;
            
            if (sp.type == GeodesicTracer::SurfacePoint::Type::FACE) {
                adjacentFaces.push_back(sp.elementId);
            }
            else if (sp.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
                uint32_t edgeId = sp.elementId;
                uint32_t he = inputConn.getEdges()[edgeId].halfEdgeIdx;
                uint32_t face1 = inputConn.getHalfEdges()[he].face;
                if (face1 != INVALID_INDEX) adjacentFaces.push_back(face1);
                
                uint32_t oppositeHe = inputConn.getHalfEdges()[he].opposite;
                if (oppositeHe != INVALID_INDEX) {
                    uint32_t face2 = inputConn.getHalfEdges()[oppositeHe].face;
                    if (face2 != INVALID_INDEX) adjacentFaces.push_back(face2);
                }
            }
            else if (sp.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
                uint32_t vertId = sp.elementId;
                auto vertexHalfedges = inputConn.getVertexHalfEdges(vertId);
                for (uint32_t he : vertexHalfedges) {
                    uint32_t faceId = inputConn.getHalfEdges()[he].face;
                    if (faceId != INVALID_INDEX) adjacentFaces.push_back(faceId);
                }
            }
            
            for (uint32_t faceId : adjacentFaces) {
                pointsByInputFace[faceId].push_back(i);
            }
        }
        
        // Simple fan triangulation for each group
        for (const auto& [inputFaceId, pointIndices] : pointsByInputFace) {
            if (pointIndices.size() < 3) 
                continue;
            
            uint32_t anchor = tri.indices[pointIndices[0]];
            uint32_t localAnchor = static_cast<uint32_t>(pointIndices[0]);
            
            for (size_t i = 1; i + 1 < pointIndices.size(); ++i) {
                uint32_t idx1 = tri.indices[pointIndices[i]];
                uint32_t idx2 = tri.indices[pointIndices[i + 1]];
                
                uint32_t localIdx1 = static_cast<uint32_t>(pointIndices[i]);
                uint32_t localIdx2 = static_cast<uint32_t>(pointIndices[i + 1]);
                
                // Skip degenerate triangles
                if (anchor == idx1 || idx1 == idx2 || idx2 == anchor) 
                    continue;
                
                glm::vec3 p0 = vertices[anchor].pos;
                glm::vec3 p1 = vertices[idx1].pos;
                glm::vec3 p2 = vertices[idx2].pos;
                float area = glm::length(glm::cross(p1 - p0, p2 - p0));
                
                if (area < 1e-8f) 
                    continue;
                
                // Set vertex colors
                vertices[anchor].color = color;
                vertices[idx1].color = color;
                vertices[idx2].color = color;
                
                // Add triangle to global indices
                indices.push_back(anchor);
                indices.push_back(idx1);
                indices.push_back(idx2);
                
                // Add triangle to local triangulation indices
                tri.triangulationIndices.push_back(localAnchor);
                tri.triangulationIndices.push_back(localIdx1);
                tri.triangulationIndices.push_back(localIdx2);
            }
        }
    }
    
    std::cout << "[CommonSubdivision] Created " << (indices.size() / 3) << " output triangles" << std::endl;
}

CommonSubdivision::IntrinsicTriangle CommonSubdivision::buildIntrinsicTriangle(uint32_t intrinsicFaceIdx) {
    const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);
    IntrinsicTriangle triangle;
    triangle.faceIdx = intrinsicFaceIdx;
    
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();
    
    if (intrinsicFaceIdx >= faces.size() || faces[intrinsicFaceIdx].halfEdgeIdx == INVALID_INDEX) {
        return triangle;
    }
    
    // Get the 3 intrinsic vertices
    auto faceVerts = conn.getFaceVertices(intrinsicFaceIdx);
    if (faceVerts.size() != 3) {
        return triangle;
    }
    
    triangle.intrinsicVertices[0] = faceVerts[0];
    triangle.intrinsicVertices[1] = faceVerts[1];
    triangle.intrinsicVertices[2] = faceVerts[2];
    
    // Get the 3 halfedges to trace
    auto faceHalfedges = conn.getFaceHalfEdges(intrinsicFaceIdx);
    if (faceHalfedges.size() != 3) {
        return triangle;
    }
    
    // Trace each edge 
    auto edge0Points = traceFn(faceHalfedges[0]);
    auto edge1Points = traceFn(faceHalfedges[1]);
    auto edge2Points = traceFn(faceHalfedges[2]);
    
    if (edge0Points.empty() || edge1Points.empty() || edge2Points.empty()) {
        return triangle;
    }
    
    // Get the 2D layout of this intrinsic triangle based on edge lengths
    auto layout2D = intrinsicMesh.layoutTriangle(intrinsicFaceIdx);
    
    auto computeBaryForPoint = [&](const GeodesicTracer::SurfacePoint& sp, int edgeHint) -> glm::dvec3 {
        glm::dvec2 pos2D;
        
        if (sp.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
            // Corner vertex 
            for (int i = 0; i < 3; i++) {
                if (sp.elementId == triangle.intrinsicVertices[i]) {
                    pos2D = layout2D.vertices[i];
                    break;
                }
            }
        } else {
            // Subdivided vertex
            glm::dvec3 pos3D = tracer.evaluateSurfacePoint(sp);
            glm::dvec3 c0 = tracer.evaluateSurfacePoint(vertexLocations.at(triangle.intrinsicVertices[edgeHint]));
            glm::dvec3 c1 = tracer.evaluateSurfacePoint(vertexLocations.at(triangle.intrinsicVertices[(edgeHint + 1) % 3]));
            
            double edgeLen3D = glm::length(c1 - c0);
            double t = (edgeLen3D > 1e-10) ? glm::length(pos3D - c0) / edgeLen3D : 0.5;
            t = glm::clamp(t, 0.0, 1.0);
            
            pos2D = glm::mix(layout2D.vertices[edgeHint], layout2D.vertices[(edgeHint + 1) % 3], t);
        }
        
        // Calculate 2D barycentric coordinates
        return intrinsicMesh.computeBarycentric2D(
            pos2D,
            layout2D.vertices[0],
            layout2D.vertices[1],
            layout2D.vertices[2]
        );
    };
    
    // Add all points from edge 0 (corner 0 -> corner 1)
    for (const auto& sp : edge0Points) {
        triangle.vertices.push_back(sp);
        triangle.positions.push_back(glm::vec3(tracer.evaluateSurfacePoint(sp)));
        triangle.baryCoords.push_back(computeBaryForPoint(sp, 0));
    }
    
    // Add all points from edge 1 except first (corner 1 -> corner 2)
    for (size_t i = 1; i < edge1Points.size(); ++i) {
        triangle.vertices.push_back(edge1Points[i]);
        triangle.positions.push_back(glm::vec3(tracer.evaluateSurfacePoint(edge1Points[i])));
        triangle.baryCoords.push_back(computeBaryForPoint(edge1Points[i], 1));
    }
    
    // Add all points from edge 2 except first and last (corner 2 -> corner 0)
    for (size_t i = 1; i + 1 < edge2Points.size(); ++i) {
        triangle.vertices.push_back(edge2Points[i]);
        triangle.positions.push_back(glm::vec3(tracer.evaluateSurfacePoint(edge2Points[i])));
        triangle.baryCoords.push_back(computeBaryForPoint(edge2Points[i], 2));
    }

    if (triangle.vertices.size() < 3) {
        std::cout << "[CommonSubdivision] Warning: Intrinsic triangle " << intrinsicFaceIdx << " has only " << triangle.vertices.size() << " vertices." << std::endl;
    }
    
    return triangle;
}

std::vector<glm::vec3> CommonSubdivision::mergeNearbyPoints(const std::vector<glm::vec3>& points, double tolerance, std::vector<uint32_t>& outMapping) const {
    if (points.empty())
        return points;
    
    std::vector<glm::vec3> merged;
    merged.reserve(points.size());
    outMapping.resize(points.size());
    
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        bool foundNearby = false;
        
        for (size_t j = 0; j < merged.size(); ++j) {
            if (glm::length(point - merged[j]) <= tolerance) {
                merged[j] = (merged[j] + point) * 0.5f;  // Average positions
                outMapping[i] = static_cast<uint32_t>(j);
                foundNearby = true;
                break;
            }
        }
        
        if (!foundNearby) {
            uint32_t newIdx = static_cast<uint32_t>(merged.size());
            merged.push_back(point);
            outMapping[i] = newIdx;
        }
    }
    
    return merged;
}

std::vector<glm::vec3> CommonSubdivision::generateFaceColors(size_t count) const {
    std::vector<glm::vec3> colors(count);
    const float goldenRatioConjugate = 0.618033988749895f;
    float hue = 0.0f;
    
    for (size_t i = 0; i < count; ++i) {
        hue = std::fmod(hue + goldenRatioConjugate, 1.0f);
        float saturation = 0.75f + 0.2f * std::sin(static_cast<float>(i) * 0.8f);
        float value = 0.75f + 0.05f * std::cos(static_cast<float>(i) * 0.65f);
        
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
        
        colors[i] = rgb + glm::vec3(m);
    }
    
    return colors;
}

std::vector<CommonSubdivision::CommonVertex> CommonSubdivision::buildVertexData() const {
    std::vector<CommonVertex> vertexData(vertices.size());
    
    // Mark all corner vertices
    for (const auto& tri : intrinsicTriangles) {
        for (size_t cornerIdx = 0; cornerIdx < 3; cornerIdx++) {
            size_t i = tri.cornerIndices[cornerIdx];
            if (i >= tri.indices.size()) 
                continue;
            
            uint32_t index = tri.indices[i];
            if (index >= vertices.size()) 
                continue;
            
            vertexData[index].isCorner = 1;
        }
    }
    
    // Set corner indices and barycentric coords for subdivision vertices
    for (const auto& tri : intrinsicTriangles) {
        if (tri.indices.size() != tri.baryCoords.size()) 
            continue;
        
        // Get the 3 corner vertex indices 
        std::array<uint32_t, 3> corners;
        for (size_t cornerIdx = 0; cornerIdx < 3; cornerIdx++) {
            size_t i = tri.cornerIndices[cornerIdx];
            if (i >= tri.indices.size()) 
                continue;
            corners[cornerIdx] = tri.indices[i];
        }
        
        // Process all vertices
        for (size_t i = 0; i < tri.indices.size(); i++) {
            uint32_t index = tri.indices[i];
            if (index >= vertices.size()) 
                continue;
            
            // Skip if this vertex is a corner (set in pass 1)
            if (vertexData[index].isCorner == 1) 
                continue;
            
            // Set corner indices and bary coords
            if (vertexData[index].corners[0] == 0) {
                vertexData[index].corners[0] = corners[0];
                vertexData[index].corners[1] = corners[1];
                vertexData[index].corners[2] = corners[2];
                vertexData[index].baryCoords = glm::vec4(
                    tri.baryCoords[i].x, 
                    tri.baryCoords[i].y, 
                    tri.baryCoords[i].z, 
                    0.0f
                );
            }
        }
    }
    
    return vertexData;
}

void CommonSubdivision::exportToModel(Model& overlayModel) const {
    overlayModel.setVertices(vertices);
    overlayModel.setIndices(indices);
    overlayModel.recreateBuffers();
}
