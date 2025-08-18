#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>

class Model;

class HalfEdgeMesh {
public:
	static const uint32_t INVALID_INDEX = static_cast<uint32_t>(-1);

	struct HalfEdge;

	struct Vertex {
		glm::vec3 position		= glm::vec3(0.0f);
		uint32_t halfEdgeIdx	= INVALID_INDEX;
		uint32_t originalIndex	= INVALID_INDEX;
	};

	struct Edge {
		uint32_t halfEdgeIdx	= INVALID_INDEX;
		double intrinsicLength	= 0.0;
		bool isOriginal			= true;

		Edge() = default;
		Edge(uint32_t heIdx) : halfEdgeIdx(heIdx) {}
	};

	struct Face {
		uint32_t halfEdgeIdx	= INVALID_INDEX;
	};

	struct HalfEdge {
		uint32_t origin			= INVALID_INDEX;
		uint32_t next			= INVALID_INDEX;
		uint32_t prev			= INVALID_INDEX;
		uint32_t opposite		= INVALID_INDEX;
		uint32_t face			= INVALID_INDEX;

		double cornerAngle		= 0.0;
		double intrinsicLength	= 0.0;  
		double signpostAngle	= 0.0;    
		bool isFeature			= false;

	};

	struct Split {
		uint32_t newV			= INVALID_INDEX;
		uint32_t heA			= INVALID_INDEX;	
		uint32_t heB			= INVALID_INDEX;	
		uint32_t diagFront		= INVALID_INDEX;
		uint32_t diagBack		= INVALID_INDEX;
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
	void buildFromModel(const class Model& srcModel);
	void applyToModel(class Model& dstModel) const;
	void initializeIntrinsicLengths();
	void rebuildEdges();
	void rebuildConnectivity();
	void rebuildFaceConnectivity(uint32_t faceIdx);
	void rebuildOpposites();
	void updateIntrinsicLength(uint32_t heIdx);
	std::array<glm::dvec2,4> layoutDiamond(uint32_t heIdx) const;
	bool isManifold() const;

	// Delaunay
	bool flipEdge(uint32_t halfEdgeIdx);
	bool isDelaunayEdge(uint32_t heIdx) const;
	int makeDelaunay(int maxInterations);

	// Refinement
	uint32_t addIntrinsicVertex();
	uint32_t splitTriangleIntrinsic(uint32_t faceIdx, double r0, double r1, double r2);
	uint32_t insertVertexAlongEdge(uint32_t edgeIdx);
	uint32_t connectVertices(uint32_t heA, uint32_t heB);
	Split splitEdgeTopo(uint32_t edgeIdx, double t);
	void removeVertex(uint32_t v);

	std::vector<uint32_t> getVertexHalfEdges(uint32_t vertexIdx) const;
	std::vector<uint32_t> getFaceHalfEdges(uint32_t faceIdx) const;
	std::vector<uint32_t> getFaceVertices(uint32_t faceIdx) const;
	std::vector<uint32_t> getNeighboringHalfEdges(uint32_t heIdx) const;
	uint32_t findEdge(uint32_t v1, uint32_t v2) const;
	uint32_t findFace(uint32_t e1, uint32_t e2) const;
	bool isBoundaryVertex(uint32_t vertexIdx) const;
	size_t countBoundaryEdges() const;

	// Iterative traversal
	uint32_t getNextAroundVertex(uint32_t halfEdgeIdx) const {
		const HalfEdge& he = halfEdges[halfEdgeIdx];
		return he.opposite != INVALID_INDEX ?
			halfEdges[he.opposite].next : INVALID_INDEX;
	}
	uint32_t getNextAroundFace(uint32_t halfEdgeIdx) const {
		return halfEdges[halfEdgeIdx].next;
	}
	
	// Getters
	std::vector<HalfEdge>& getHalfEdges() {  
		return halfEdges;
	}
	const std::vector<HalfEdge>& getHalfEdges() const {
		return halfEdges;
	}
	const std::vector<Vertex>& getVertices() const {
		return vertices;
	}
	std::vector<Vertex>& getVertices() {	 
		return vertices;
	}
	const std::vector<Edge>& getEdges() const {
		return edges;
	}
	std::vector<Edge>& getEdges() {			 
		return edges;
	}
	const std::vector<Face>& getFaces() const {
		return faces;
	}
	std::vector<Face>& getFaces() {
		return faces;
	}

	glm::vec3 getPositionFromHalfEdge(uint32_t halfEdgeIdx) const {
		return vertices[halfEdges[halfEdgeIdx].origin].position;
	}

	double getIntrinsicLengthFromHalfEdge(uint32_t halfEdgeIdx) const {
		return halfEdgeIdx < halfEdges.size() ? halfEdges[halfEdgeIdx].intrinsicLength : 0.0;
	}

	uint32_t getEdgeIndexFromHalfEdge(uint32_t halfEdgeIdx) const;

	std::vector<glm::vec3> getVertexPositions() const;

	// Setters
	void setVertexPositions(const std::vector<glm::vec3>& newPositions);

	// Debug
	void debugPrintStats() const;
	
private:
	std::vector<HalfEdge> halfEdges;
	std::vector<Vertex> vertices;
	std::vector<Edge> edges;
	std::vector<Face> faces;
};