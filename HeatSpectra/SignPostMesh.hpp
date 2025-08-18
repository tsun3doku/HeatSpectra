#pragma once
#include <glm/glm.hpp>

#include <vector>
#include <unordered_set>

#include "HalfEdgeMesh.hpp"

class Model;

class SignpostMesh {
public:
    static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

    struct Triangle2D {
        glm::dvec2 vertices[3]          = { glm::dvec2(0,0), glm::dvec2(0,0), glm::dvec2(0,0) };
        uint32_t indices[3]             = { 0,0,0 };
        double edgeLengths[3]           = { 0,0,0 };
    };

    struct pair_hash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    // Construction
    void buildFromModel(const Model& srcModel);
    void applyToModel(Model& dstModel) const;
    void initializeIntrinsicGeometry();
    Triangle2D layoutTriangle(uint32_t faceIdx) const;

    // Intrinsic operations
    void updateSignpostAngles(uint32_t vertexIdx);
    void updateAllSignposts();
    void buildHalfedgeVectorsInVertex();
    void buildHalfedgeVectorsInFace();

    // Intrinsic helpers
    glm::dvec2 computeCircumcenter2D(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const;
    glm::dvec3 computeBarycentric2D(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) const;
    double computeSplitDiagonalLength(uint32_t faceIdx, uint32_t originalVA, uint32_t originalVB, double splitFraction) const;

    // Angle operations
    double computeAngleFromLengths(double a, double b, double c, uint32_t faceIdx) const;
    void updateCornerAnglesForFace(uint32_t faceIdx);
    void updateAllCornerAngles(const std::unordered_set<uint32_t>& skipFaces);

    // Face operations
    float computeFaceArea(uint32_t faceIdx) const;
    std::vector<float> getAllFaceAreas() const;

    // Edge operations  
    bool isEdgeOnBoundary(uint32_t heIdx) const;

    // Vertex operations
    bool isBoundaryVertex(uint32_t vertexIdx) const;
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
    const std::vector<glm::dvec2>& getHalfedgeVectorsInFace() const {
        return halfedgeVectorsInFace;
    }
    double getCornerAngle(uint32_t halfEdgeIdx) const;

private:
    HalfEdgeMesh conn;
    std::vector<glm::vec3> faceNormals;
    std::vector<double> vertexAngleSums;
    std::vector<glm::dvec2> halfedgeVectorsInVertex;
    std::vector<glm::dvec2> halfedgeVectorsInFace;
};