#pragma once
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <unordered_set>

#include "HalfEdgeMesh.hpp"

class SignpostMesh {
public:
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    using Triangle2D = HalfEdgeMesh::Triangle2D;

    // Construction
    void buildFromIndexedData(
        const std::vector<float>& pointPositions,
        const std::vector<uint32_t>& triangleIndices);
    Triangle2D layoutTriangle(uint32_t faceIdx) const;

    // Intrinsic operations
    void updateAllSignposts();
    glm::dvec2 halfedgeVector(uint32_t heIdx) const;
    void buildHalfedgeVectorsInVertex();
    void updateVertexVectors(const std::vector<uint32_t>& vertexIndices);
    void buildHalfedgeVectorsInFace();
    void updateFaceVectors(const std::vector<uint32_t>& faceIndices);

    // Intrinsic helpers
    glm::dvec2 computeCircumcenter2D(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const;
    glm::dvec3 computeBarycentric2D(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const;
    double computeSplitDiagonalLength(uint32_t faceIdx, uint32_t originalVA, uint32_t originalVB, double splitFraction) const;

    // Angle operations
    void rebuildVertexSums();
    void updateVertexSums(const std::vector<uint32_t>& vertexIndices);
    double computeAngleFromLengths(double a, double b, double c, uint32_t faceIdx) const;
    std::array<double, 3> computeCornerAngles(uint32_t faceIdx) const;
    void computeCornerScaledAngles();
    void updateCornerScales(const std::vector<uint32_t>& vertexIndices);
    void computeVertexAngleScales();
    void updateVertexScales(const std::vector<uint32_t>& vertexIndices);
    void updateCornerAnglesForFace(uint32_t faceIdx);
    void updateAllCornerAngles(const std::unordered_set<uint32_t>& skipFaces);
    
    // Signpost angle operations
    double standardizeAngleForVertex(uint32_t vertexIdx, double angleRad) const;
    void updateAngleFromCWNeighbor(uint32_t heIdx);
    
    // Face operations
    float computeFaceArea(uint32_t faceIdx) const;
    std::vector<float> getAllFaceAreas() const;

    // Edge operations  
    bool isEdgeOnBoundary(uint32_t heIdx) const;

    // Vertex operations
    std::vector<uint32_t> getBoundaryVertices() const;
    uint32_t getVertexDegree(uint32_t vertexIdx) const;

    // Debug
    void printMeshStatistics() const;

   // Getters
    const HalfEdgeMesh& getConnectivity() const {
        return conn;
    }
    HalfEdgeMesh& getConnectivity() {
        return conn;
    }
    const std::vector<glm::dvec2>& getHalfedgeVectorsInVertex() const { 
        return halfedgeVectorsInVertex; 
    }
    std::vector<glm::dvec2>& getHalfedgeVectorsInVertex() {
        return halfedgeVectorsInVertex;
    }
    const std::vector<glm::dvec2>& getHalfedgeVectorsInFace() const {
        return halfedgeVectorsInFace;
    }
    const std::vector<double>& getVertexAngleSums() const {
        return vertexAngleSums;
    }
    std::vector<double>& getVertexAngleSums() {
        return vertexAngleSums;
    }
    const std::vector<double>& getVertexAngleScales() const {
        return vertexAngleScales;
    }
    std::vector<double>& getVertexAngleScales() {
        return vertexAngleScales;
    }
    double getVertexAngleSum(uint32_t vertexIdx) const {
        return (vertexIdx < vertexAngleSums.size()) ? vertexAngleSums[vertexIdx] : 0.0;
    }
    double getCornerAngle(uint32_t halfEdgeIdx) const;

private:
    HalfEdgeMesh conn;
    std::vector<glm::vec3> faceNormals;
    std::vector<double> vertexAngleSums;
    std::vector<double> vertexAngleScales;  
    std::vector<double> cornerScaledAngles;
    std::vector<glm::dvec2> halfedgeVectorsInVertex;
    std::vector<glm::dvec2> halfedgeVectorsInFace;
};
