/**
 * HalfEdgeMesh - A halfedge data structure for representation of mesh connectivity
 *
 * This implementation assumes manifold input meshes.
 */

#include <unordered_map>
#include <queue>
#include <utility> 
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>

#include "Model.hpp"
#include "HalfEdgeMesh.hpp"

void HalfEdgeMesh::buildFromModel(const class Model& srcModel) {
	vertices.clear();
	edges.clear();
	faces.clear();
	halfEdges.clear();

	// Create vertices
	size_t vertexCount = srcModel.getVertexCount();
	vertices.resize(vertexCount);
	for (size_t i = 0; i < vertexCount; ++i) {
		vertices[i].position = srcModel.getVertices()[i].pos;
		vertices[i].originalIndex = static_cast<uint32_t>(i);
		vertices[i].halfEdgeIdx = INVALID_INDEX;
	}

	// Create faces and halfedges
	const std::vector<uint32_t>& indices = srcModel.getIndices();
	size_t triangleCount = indices.size() / 3;

	// Pre-allocate space 
	halfEdges.reserve(triangleCount * 3);
	faces.reserve(triangleCount);

	// Create a map to store pairs of vertices to their halfedge index
	std::unordered_map<std::pair<uint32_t, uint32_t>, uint32_t, pair_hash> halfEdgeMap;

	// For each triangle
	for (size_t i = 0; i < triangleCount; ++i) {
		uint32_t idx0 = indices[i * 3];
		uint32_t idx1 = indices[i * 3 + 1];
		uint32_t idx2 = indices[i * 3 + 2];

		// Check for degenerate triangles 
		if (idx0 == idx1 || idx1 == idx2 || idx2 == idx0) {
			//std::cerr << "Skipping degenerate triangle " << i << " with vertices [" << idx0 << ", " << idx1 << ", " << idx2 << "]\n";
			continue;
		}

		// Create a new face
		Face face;
		uint32_t faceIdx = static_cast<uint32_t>(faces.size());
		uint32_t he0Idx = static_cast<uint32_t>(halfEdges.size());
		uint32_t he1Idx = he0Idx + 1;
		uint32_t he2Idx = he0Idx + 2;

		// Create three halfedges for this face
		HalfEdge he0, he1, he2;

		// Set origin vertex for each halfedge
		he0.origin = idx0;
		he1.origin = idx1;
		he2.origin = idx2;

		// Set face for each halfedge
		he0.face = faceIdx;
		he1.face = faceIdx;
		he2.face = faceIdx;

		// Set halfedge connectivity
		he0.next = he1Idx;
		he1.next = he2Idx;
		he2.next = he0Idx;

		he0.prev = he2Idx;
		he1.prev = he0Idx;
		he2.prev = he1Idx;

		// Set halfedge for face
		face.halfEdgeIdx = he0Idx;

		// Store halfedge index for each vertex pair
		halfEdgeMap[{idx0, idx1}] = he0Idx;
		halfEdgeMap[{idx1, idx2}] = he1Idx;
		halfEdgeMap[{idx2, idx0}] = he2Idx;

		// Set halfedge indices for vertices 
		vertices[idx0].halfEdgeIdx = he0Idx;  // he0 originates from idx0
		vertices[idx1].halfEdgeIdx = he1Idx;  // he1 originates from idx1  
		vertices[idx2].halfEdgeIdx = he2Idx;  // he2 originates from idx2

		// Add the halfedges and face to data structures
		halfEdges.push_back(he0);
		halfEdges.push_back(he1);
		halfEdges.push_back(he2);
		faces.push_back(face);
	}

	// Connect opposite halfedges
	for (auto& pair : halfEdgeMap) {
		uint32_t v1 = pair.first.first;
		uint32_t v2 = pair.first.second;
		uint32_t heIdx = pair.second;

		// Find opposite half edge
		auto oppositeIt = halfEdgeMap.find({ v2, v1 });
		if (oppositeIt != halfEdgeMap.end()) {
			halfEdges[heIdx].opposite = oppositeIt->second;
		}
	}

	// Create edges in triangle order
	edges.reserve(halfEdges.size() / 2);
	
	std::vector<std::pair<uint32_t, uint32_t>> edgeOrder;
	std::set<std::pair<uint32_t, uint32_t>> seenEdges;
	
	// Process triangles in order, adding edges as first encountered
	const std::vector<uint32_t>& modelIndices = srcModel.getIndices();
	for (size_t triangleIdx = 0; triangleIdx < modelIndices.size() / 3; ++triangleIdx) {
		uint32_t v0 = modelIndices[triangleIdx * 3];
		uint32_t v1 = modelIndices[triangleIdx * 3 + 1];
		uint32_t v2 = modelIndices[triangleIdx * 3 + 2];
		
		// Check each edge of this triangle: (v0,v1), (v1,v2), (v2,v0)
		std::vector<std::pair<uint32_t, uint32_t>> triangleEdges = {
			{v0, v1}, {v1, v2}, {v2, v0}
		};
		
		for (const auto& directedEdge : triangleEdges) {
			auto unorderedEdge = std::minmax(directedEdge.first, directedEdge.second);
			
			if (seenEdges.find(unorderedEdge) == seenEdges.end()) {
				seenEdges.insert(unorderedEdge);
				edgeOrder.push_back(directedEdge);
			}
		}
	}
	
	// Create Edge objects
	for (const auto& directedEdge : edgeOrder) {
		uint32_t v1 = directedEdge.first;
		uint32_t v2 = directedEdge.second;
		
		// Look up the halfedge
		uint32_t foundHE = INVALID_INDEX;
		auto it = halfEdgeMap.find({v1, v2});
		if (it != halfEdgeMap.end()) {
			foundHE = it->second;
		} else {
			auto reverseIt = halfEdgeMap.find({v2, v1});
			if (reverseIt != halfEdgeMap.end()) {
				foundHE = reverseIt->second;
			}
		}
		
		if (foundHE != INVALID_INDEX) {
			uint32_t newEdgeIdx = static_cast<uint32_t>(edges.size());
			edges.emplace_back(foundHE);
			
			// Set both halfedges of this edge to point to the same edge index
			halfEdges[foundHE].edgeIdx = newEdgeIdx;
			uint32_t oppositeHE = halfEdges[foundHE].opposite;
			if (oppositeHE != INVALID_INDEX) {
				halfEdges[oppositeHE].edgeIdx = newEdgeIdx;
			}
		}
	}

	if (!isManifold()) {
		throw std::runtime_error("Mesh is not manifold");
	}

	initializeIntrinsicLengths();
}

void HalfEdgeMesh::applyToModel(class Model& dstModel) const {
	std::vector<::Vertex> newVertices;
	std::vector<uint32_t> newIndices;

	// Map HalfEdgeMesh vertex indices to new indices
	std::unordered_map<uint32_t, uint32_t> vertexIndexMap;

	newVertices.reserve(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i) {
		const auto& heVertex = vertices[i];

		::Vertex modelVertex;
		modelVertex.pos = heVertex.position;

		if (heVertex.originalIndex < dstModel.getVertexCount()) {
			const auto& originalVertex = dstModel.getVertices()[heVertex.originalIndex];
			modelVertex.color = originalVertex.color;
			modelVertex.normal = originalVertex.normal;
			modelVertex.texCoord = originalVertex.texCoord;
		}
		else {
			// New vertex
			modelVertex.color = glm::vec3(0.0f, 0.0f, 0.0f);
			modelVertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
			modelVertex.texCoord = glm::vec2(0.0f);
		}

		vertexIndexMap[i] = static_cast<uint32_t>(newVertices.size());
		newVertices.push_back(modelVertex);
	}

	newIndices.reserve(faces.size() * 3);
	for (size_t i = 0; i < faces.size(); ++i) {
		const auto& face = faces[i];
		if (face.halfEdgeIdx == INVALID_INDEX)
			continue;

		std::vector<uint32_t> faceHalfEdges = getFaceHalfEdges(i);

		// Skip degenerate faces
		if (faceHalfEdges.size() < 3)
			continue;

		if (faceHalfEdges.size() == 3) {
			for (uint32_t heIdx : faceHalfEdges) {
				uint32_t vertexIdx = halfEdges[heIdx].origin;
				newIndices.push_back(vertexIndexMap[vertexIdx]);
			}
		}
		else {
			// Triangulate for non-triangular faces
			uint32_t firstVertexIdx = halfEdges[faceHalfEdges[0]].origin;

			for (size_t j = 1; j < faceHalfEdges.size() - 1; ++j) {
				uint32_t v1Idx = halfEdges[faceHalfEdges[j]].origin;
				uint32_t v2Idx = halfEdges[faceHalfEdges[j + 1]].origin;

				newIndices.push_back(vertexIndexMap[firstVertexIdx]);
				newIndices.push_back(vertexIndexMap[v1Idx]);
				newIndices.push_back(vertexIndexMap[v2Idx]);
			}
		}
	}

	dstModel.updateGeometry(newVertices, newIndices);
	dstModel.recalculateNormals();
}

void HalfEdgeMesh::initializeIntrinsicLengths() {
	// Initialize intrinsic lengths
	for (auto& e : edges) {
		uint32_t heIdx = e.halfEdgeIdx;
		if (heIdx >= halfEdges.size()) continue;
		
		// Skip any bad halfedges
		if (halfEdges[heIdx].next == INVALID_INDEX || halfEdges[heIdx].origin == INVALID_INDEX)
			continue;

		uint32_t v0 = halfEdges[heIdx].origin;
		uint32_t v1 = halfEdges[halfEdges[heIdx].next].origin;
		if (v1 == INVALID_INDEX) continue;

		const glm::vec3& p0 = vertices[v0].position;
		const glm::vec3& p1 = vertices[v1].position;

		glm::dvec3 dp0(p0.x, p0.y, p0.z);
		glm::dvec3 dp1(p1.x, p1.y, p1.z);
		double length = glm::length(dp1 - dp0);
		e.intrinsicLength = length;		
	}
}

void HalfEdgeMesh::rebuildFaceConnectivity(uint32_t faceIdx) {
	if (faceIdx == INVALID_INDEX) 
		return;
	uint32_t start = faces[faceIdx].halfEdgeIdx;
	if (start == INVALID_INDEX) 
		return;
	uint32_t cur = start;
	do {
		halfEdges[cur].face = faceIdx;
		cur = halfEdges[cur].next;
	} while (cur != start && cur != INVALID_INDEX);
}

HalfEdgeMesh::Triangle2D HalfEdgeMesh::layoutTriangle(uint32_t faceIdx) const {
	HalfEdgeMesh::Triangle2D result;

	// Get halfedges of face
	uint32_t he0 = faces[faceIdx].halfEdgeIdx;
	if (he0 == INVALID_INDEX) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}

	uint32_t he1 = halfEdges[he0].next;
	uint32_t he2 = halfEdges[he1].next;
	if (halfEdges[he2].next != he0) {
		// Not a triangle
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}

	// Vertex indices
	result.indices[0] = halfEdges[he0].origin;
	result.indices[1] = halfEdges[he1].origin;
	result.indices[2] = halfEdges[he2].origin;

	// Edge lengths
	result.edgeLengths[0] = edges[getEdgeFromHalfEdge(he0)].intrinsicLength;
	result.edgeLengths[1] = edges[getEdgeFromHalfEdge(he1)].intrinsicLength;
	result.edgeLengths[2] = edges[getEdgeFromHalfEdge(he2)].intrinsicLength;

	// Validate
	const double MIN_LENGTH = 1e-12;
	if (result.edgeLengths[0] < MIN_LENGTH ||
		result.edgeLengths[1] < MIN_LENGTH ||
		result.edgeLengths[2] < MIN_LENGTH) {
		//std::cerr << "[layoutTriangle] Degenerate edge in face " << faceIdx << "\n";
		return result;
	}

	const double EPS = 1e-12;
	double a = result.edgeLengths[0], b = result.edgeLengths[1], c = result.edgeLengths[2];
	if (!(a + b > c + EPS && a + c > b + EPS && b + c > a + EPS)) {
		//std::cerr << "[layoutTriangle] Triangle inequality failed in face " << faceIdx << ": " << a << ", " << b << ", " << c << "\n";
		return result;
	}

	// Canonical layout: v0=(0,0), v1=(a,0), v2=(x,y)
	result.vertices[0] = glm::dvec2(0.0, 0.0);
	result.vertices[1] = glm::dvec2(a, 0.0);

	double x = (a * a + c * c - b * b) / (2.0 * a);
	double y2 = c * c - x * x;
	double y = (y2 > 0.0) ? std::sqrt(y2) : 0.0;
	result.vertices[2] = glm::dvec2(x, y);

	return result;
}

std::array<glm::dvec2, 4> HalfEdgeMesh::layoutDiamond(uint32_t heIdx) const {
	const auto& HEs = halfEdges;

	uint32_t opp0 = HEs[heIdx].opposite;
	if (opp0 == INVALID_INDEX) {
		//std::cerr << "[layoutDiamond] Invalid opposite for he" << heIdx << "\n";
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	uint32_t fa = HEs[heIdx].face;
	uint32_t fb = HEs[opp0].face;

	if (fa == INVALID_INDEX || fb == INVALID_INDEX) {
		//std::cerr << "[layoutDiamond] Invalid face for he" << heIdx << " or its twin\n";
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	// Get halfedges and vertices
	uint32_t he0 = heIdx;
	uint32_t he1 = HEs[he0].next;
	uint32_t he2 = HEs[he1].next;
	uint32_t opp1 = HEs[opp0].next;
	uint32_t opp2 = HEs[opp1].next;

	uint32_t va = HEs[he0].origin;
	uint32_t vb = HEs[opp0].origin;
	uint32_t vc = HEs[he2].origin;
	uint32_t vd = HEs[opp2].origin;

	Triangle2D triA = layoutTriangle(fa);
	Triangle2D triB = layoutTriangle(fb);

	if (triA.edgeLengths[0] == 0.0 || triB.edgeLengths[0] == 0.0) {
		//std::cerr << "[layoutDiamond] Triangle layout failed for face " << fa << " or " << fb << "\n";
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	// Get intrinsic lengths for the diamond edges
	double diagLen = edges[getEdgeFromHalfEdge(he0)].intrinsicLength;		// va-vb
	double len_vb_vc = edges[getEdgeFromHalfEdge(he1)].intrinsicLength;		// vb-vc
	double len_vc_va = edges[getEdgeFromHalfEdge(he2)].intrinsicLength;		// vc-va
	double len_va_vd = edges[getEdgeFromHalfEdge(opp1)].intrinsicLength;	// va-vd
	double len_vd_vb = edges[getEdgeFromHalfEdge(opp2)].intrinsicLength;	// vd-vb

	// Layout: place va at (0,0), vb at (diagLen, 0)
	glm::dvec2 p1(0.0, 0.0);          // va
	glm::dvec2 p2(diagLen, 0.0);      // vb

	// Calculate vc position using triangle (va, vb, vc)
	double x_vc = (diagLen * diagLen + len_vc_va * len_vc_va - len_vb_vc * len_vb_vc) / (2.0 * diagLen);
	double y2_vc = len_vc_va * len_vc_va - x_vc * x_vc;
	if (y2_vc < 0.0) {
		//std::cerr << "[layoutDiamond] Triangle A (va,vb,vc) is invalid or degenerate\n";
		y2_vc = 0.0;
	}

	// Place above x-axis
	double y_vc = std::sqrt(y2_vc);   
	glm::dvec2 p3(x_vc, y_vc);        // vc

	// Calculate vd position using triangle (va, vb, vd)
	double x_vd = (diagLen * diagLen + len_va_vd * len_va_vd - len_vd_vb * len_vd_vb) / (2.0 * diagLen);
	double y2_vd = len_va_vd * len_va_vd - x_vd * x_vd;
	if (y2_vd < 0.0) {
		//std::cerr << "[layoutDiamond] Triangle B (va,vb,vd) is invalid or degenerate\n";
		y2_vd = 0.0;
	}

	// Place below x-axis for diamond layout
	double y_vd = -std::sqrt(y2_vd);  
	glm::dvec2 p4(x_vd, y_vd);        

	// Calculate areas for debugging
	auto area2D = [&](const glm::dvec2& A, const glm::dvec2& B, const glm::dvec2& C) {
		return std::abs((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x)) * 0.5;
		};

	double area1 = area2D(p1, p2, p3);
	double area2 = area2D(p1, p2, p4);
	const double MIN_AREA = 1e-16;

	// Debug
	double dist = glm::distance(p1, p2);
	double x1 = (dist * dist + len_vc_va * len_vc_va - len_vb_vc * len_vb_vc) / (2.0 * dist);
	double y2_1 = len_vc_va * len_vc_va - x1 * x1;
	double x2 = (dist * dist + len_va_vd * len_va_vd - len_vd_vb * len_vd_vb) / (2.0 * dist);
	double y2_2 = len_va_vd * len_va_vd - x2 * x2;

	if (area1 < MIN_AREA || area2 < MIN_AREA) {
		//std::cerr << "[layoutDiamond] he=" << heIdx << " near-zero area detected\n";
		//std::cerr << "  quad corners: va=" << va << " vb=" << vb << " vc=" << vc << " vd=" << vd << "\n";		
	}

	if (area1 < MIN_AREA) {
		//std::cout << "[layoutDiamond] Near-zero area1 (" << area1 << ") he=" << heIdx << "\n";
	}
	if (area2 < MIN_AREA) {
		//std::cout << "[layoutDiamond] Near-zero area2 (" << area2 << ") he=" << heIdx << " at p4=(" << p4.x << "," << p4.y << ")\n";
	}
	/*
	// DEBUG: Final positions
	std::cout << "[layoutDiamond] Layout: p1=(" << p1.x << "," << p1.y << ") "
		<< "p2=(" << p2.x << "," << p2.y << ") "
		<< "p3=(" << p3.x << "," << p3.y << ") "
		<< "p4=(" << p4.x << "," << p4.y << ")\n";
	*/
	return { p1, p2, p3, p4 };
}

HalfEdgeMesh::VertexRing2D HalfEdgeMesh::buildVertexRing2D(uint32_t vertexIdx) const {
	VertexRing2D ring;
	ring.centerVertexIdx = vertexIdx;

	// Get all outgoing halfedges in CCW order
	auto outgoingHEs = getVertexHalfEdges(vertexIdx);
	if (outgoingHEs.empty()) {
		return ring;
	}

	// Extract neighbor info
	for (uint32_t heOut : outgoingHEs) {
		uint32_t nextHe = halfEdges[heOut].next;
		if (nextHe == INVALID_INDEX) 
			continue;
		
		uint32_t neighborIdx = halfEdges[nextHe].origin;
		uint32_t edgeIdx = getEdgeFromHalfEdge(heOut);
		uint32_t faceIdx = halfEdges[heOut].face;
		
		if (edgeIdx != INVALID_INDEX) {
			ring.neighborVertexIndices.push_back(neighborIdx);
			ring.edgeIndices.push_back(edgeIdx);
			if (faceIdx != INVALID_INDEX) {
				ring.faceIndices.push_back(faceIdx);
			}
		}
	}

	if (ring.neighborVertexIndices.empty()) {
		return ring;
	}

	// Lay out neighbors in 2D with center at origin
	// Place first neighbor on positive x-axis
	uint32_t firstEdge = ring.edgeIndices[0];
	double firstLen = edges[firstEdge].intrinsicLength;
	ring.neighborPositions2D.push_back(glm::dvec2(firstLen, 0.0));

	// Unfold remaining neighbors using corner angles
	for (size_t i = 1; i < ring.neighborVertexIndices.size(); ++i) {
		uint32_t edgeIdx = ring.edgeIndices[i];
		double edgeLen = edges[edgeIdx].intrinsicLength;
		
		// Get cumulative angle from first edge
		double cumulativeAngle = 0.0;
		for (size_t j = 0; j < i; ++j) {
			uint32_t heOut = outgoingHEs[j];
			cumulativeAngle += halfEdges[heOut].cornerAngle;
		}
		
		// Position = rotate by cumulative angle
		glm::dvec2 pos;
		pos.x = edgeLen * std::cos(cumulativeAngle);
		pos.y = edgeLen * std::sin(cumulativeAngle);
		ring.neighborPositions2D.push_back(pos);
	}

	return ring;
}

bool HalfEdgeMesh::isManifold() const {
	// Add a safety counter to prevent infinite loops
	const int MAX_ITERATIONS = 1000;

	// Edge manifoldness
	for (const Edge& edge : edges) {
		uint32_t heIdx = edge.halfEdgeIdx;
		if (heIdx >= halfEdges.size()) continue;

		uint32_t v1Idx = halfEdges[heIdx].origin;

		// Get the destination vertex safely
		uint32_t nextHeIdx = halfEdges[heIdx].next;
		if (nextHeIdx >= halfEdges.size()) continue;

		uint32_t v2Idx = halfEdges[nextHeIdx].origin;

		// Count outgoing edges between these vertices
		int connections = 0;
		uint32_t startIdx = vertices[v1Idx].halfEdgeIdx;
		uint32_t currentIdx = startIdx;
		int safetyCounter = 0;

		while (currentIdx != INVALID_INDEX && safetyCounter < MAX_ITERATIONS) {
			safetyCounter++;

			if (currentIdx >= halfEdges.size()) break;
			const HalfEdge& current = halfEdges[currentIdx];

			uint32_t nextIdx = current.next;
			if (nextIdx >= halfEdges.size()) break;

			const HalfEdge& next = halfEdges[nextIdx];

			if (next.origin == v2Idx)
				connections++;

			if (current.opposite == INVALID_INDEX)
				break;

			currentIdx = halfEdges[current.opposite].next;

			if (currentIdx == startIdx)
				break;
		}

		if (connections > 2 || safetyCounter >= MAX_ITERATIONS)
			return false;
	}

	// Vertex manifoldness
	for (size_t i = 0; i < vertices.size(); ++i) {
		const Vertex& vert = vertices[i];
		if (vert.halfEdgeIdx == INVALID_INDEX)
			continue;  // Isolated vertex

		std::unordered_set<uint32_t> visited;
		uint32_t startIdx = vert.halfEdgeIdx;
		uint32_t currentIdx = startIdx;
		int safetyCounter = 0;

		// Traverse the vertex's edge fan
		while (currentIdx != INVALID_INDEX && !visited.count(currentIdx) && safetyCounter < MAX_ITERATIONS) {
			safetyCounter++;

			if (currentIdx >= halfEdges.size()) break;
			visited.insert(currentIdx);

			const HalfEdge& current = halfEdges[currentIdx];
			if (current.opposite == INVALID_INDEX)
				break;

			currentIdx = halfEdges[current.opposite].next;

			if (currentIdx == startIdx)
				break;
		}

		// Check if all edges were visited in a single loop
		uint32_t checkIdx = vert.halfEdgeIdx;
		safetyCounter = 0;

		while (checkIdx != INVALID_INDEX && safetyCounter < MAX_ITERATIONS) {
			safetyCounter++;

			if (checkIdx >= halfEdges.size()) break;

			if (!visited.count(checkIdx))
				return false;  

			const HalfEdge& check = halfEdges[checkIdx];
			if (check.opposite == INVALID_INDEX)
				break;

			checkIdx = halfEdges[check.opposite].next;

			if (checkIdx == vert.halfEdgeIdx)
				break;
		}

		if (safetyCounter >= MAX_ITERATIONS)
			return false;
	}

	if (!vertices.empty() && (faces.empty() || edges.empty())) {
		return false;
	}

	return true;
}

bool HalfEdgeMesh::flipEdge(uint32_t edgeIdx) {
	//std::cout << "[flipEdge] Calling flipEdge..." << std::endl;

	if (edgeIdx >= edges.size()) {
		//std::cerr << "[flipEdge] Invalid edge index: " << edgeIdx << "\n";
		return false;
	}
	
	uint32_t diagonalHE = edges[edgeIdx].halfEdgeIdx;
	if (diagonalHE >= halfEdges.size()) {
		//std::cerr << "[flipEdge] Edge " << edgeIdx << " has invalid halfedge: " << diagonalHE << "\n";
		return false;
	}
	uint32_t diagonal2HE = halfEdges[diagonalHE].opposite;
	if (diagonal2HE == INVALID_INDEX) {
		//std::cerr << "[flipEdge] Boundary edge (no opposite): " << diagonalHE << "\n";
		return false;
	}

	// Get the halfedges of both triangles 
	uint32_t ha1 = diagonalHE;
	uint32_t ha2 = halfEdges[ha1].next;
	uint32_t ha3 = halfEdges[ha2].next;
	if (ha3 >= halfEdges.size() || halfEdges[ha3].next != ha1) {
		//std::cerr << "[flipEdge] Face A not a triangle for he" << ha1 << "\n";
		return false;
	}
	uint32_t hb1 = diagonal2HE;
	uint32_t hb2 = halfEdges[hb1].next;
	uint32_t hb3 = halfEdges[hb2].next;
	if (hb3 >= halfEdges.size() || halfEdges[hb3].next != hb1) {
		//std::cerr << "[flipEdge] Face B not a triangle for he" << hb1 << "\n";
		return false;
	}

	// Manifold checks 
	if (halfEdges[hb1].opposite != ha1) {
		//std::cerr << "[flipEdge] Non-manifold pairing \n";
		return false;
	}
	if (ha2 == hb1 || hb2 == ha1) {
		//std::cerr << "[flipEdge] Incident degree 1 vertex prevents flip\n";
		return false;
	}

	// Vertices and faces
	uint32_t va = halfEdges[ha1].origin;
	uint32_t vb = halfEdges[hb1].origin;
	uint32_t vc = halfEdges[ha3].origin;
	uint32_t vd = halfEdges[hb3].origin;

	// Prevent trivials
	if (va == vb || va == vc || va == vd || vb == vc || vb == vd || vc == vd) {
		//std::cerr << "[flipEdge] Duplicate vertices in quad [" << va << "," << vb << "," << vc << "," << vd << "], skipping\n";
		return false;
	}

	uint32_t fa = halfEdges[ha1].face;
	uint32_t fb = halfEdges[hb1].face;

	/*
	// DEBUG
	std::cout << "[flipEdge] Quad verts: v0=" << va
		<< ", v1=" << vb
		<< ", v2=" << vc
		<< ", v3=" << vd << "\n";
	std::cout << "[flipEdge] Flipping diagonal he" << ha1 << " (faces " << fa << "," << fb << ")\n";
	std::cout << "[flipEdge] Current diagonal: (" << va << "->" << vb << ")\n";
	std::cout << "[flipEdge] Will become: (" << vc << "->" << vd << ")\n";
	*/

	double newLength = 0.0;
	{
		// Layout the diamond using current intrinsic lengths 
		auto positions = layoutDiamond(diagonalHE);
		// Compute new edge length 
		newLength = glm::distance(positions[2], positions[3]);
		
		//std::cout << "[flipEdge] Diamond layout: p2=(" << positions[2].x << "," << positions[2].y << ") p3=(" << positions[3].x << "," << positions[3].y << ")\n";
		// Get old diagonal length from edge record
		int oldDiagEdgeIdx = getEdgeFromHalfEdge(diagonalHE);
		double oldLength = (oldDiagEdgeIdx != -1) ? edges[oldDiagEdgeIdx].intrinsicLength : 0.0;
		//std::cout << "[flipEdge] Old diagonal length: " << oldLength << " -> New length: " << newLength << "\n";
	}

	// Check if new length is valid
	if (!std::isfinite(newLength) || newLength < 1e-10) {
		//std::cerr << "[flipEdge] Invalid new length (" << newLength << "), aborting flip\n";
		return false;
	}

	// Lengths of the boundary edges of the diamond
	double lenAC = getIntrinsicLengthFromHalfEdge(ha3);
	double lenCB = getIntrinsicLengthFromHalfEdge(ha2);
	double lenBD = getIntrinsicLengthFromHalfEdge(hb3);
	double lenDA = getIntrinsicLengthFromHalfEdge(hb2);
	// New diagonal length
	double lenCD = newLength;

	// Helper lambda to compute angle using law of cosines
	auto law_of_cosines = [](double a, double b, double opp) {
		if (a < 1e-12 || b < 1e-12) return 0.0;
		double q = (a * a + b * b - opp * opp) / (2.0 * a * b);
		q = std::max(-1.0, std::min(1.0, q)); 
		return std::acos(q);
		};

	// Update corner angles
	halfEdges[ha1].cornerAngle = law_of_cosines(lenCD, lenCB, lenBD); // Corner at vc in face fa
	halfEdges[hb3].cornerAngle = law_of_cosines(lenBD, lenCD, lenCB); // Corner at vd in face fa
	halfEdges[ha2].cornerAngle = law_of_cosines(lenCB, lenBD, lenCD); // Corner at vb in face fa

	halfEdges[hb1].cornerAngle = law_of_cosines(lenCD, lenDA, lenAC); // Corner at vd in face fb
	halfEdges[ha3].cornerAngle = law_of_cosines(lenAC, lenCD, lenDA); // Corner at vc in face fb
	halfEdges[hb2].cornerAngle = law_of_cosines(lenDA, lenAC, lenCD); // Corner at va in face fb

	// Update face's halfedges
	faces[fa].halfEdgeIdx = ha1;
	faces[fb].halfEdgeIdx = hb1;

	// Rewire next pointers 
	halfEdges[ha1].next = hb3;
	halfEdges[hb3].next = ha2;
	halfEdges[ha2].next = ha1;

	halfEdges[hb1].next = ha3;
	halfEdges[ha3].next = hb2;
	halfEdges[hb2].next = hb1;

	// Update halfedges' face assignments
	halfEdges[ha3].face = fb;
	halfEdges[hb3].face = fa;

	// Update prev pointers 
	halfEdges[ha1].prev = ha2;
	halfEdges[ha2].prev = hb3;
	halfEdges[hb3].prev = ha1;
	halfEdges[hb1].prev = hb2;
	halfEdges[hb2].prev = ha3;
	halfEdges[ha3].prev = hb1;

	// Update halfedge origins
	halfEdges[ha1].origin = vc;
	halfEdges[hb1].origin = vd;

	// Update vertex anchors
	// vc and vd cant be invalidated by the flip since they keep their outgoing halfedges
	if (vertices[va].halfEdgeIdx == ha1) {
		vertices[va].halfEdgeIdx = hb2;  // hb2 now originates FROM va
	}
	if (vertices[vb].halfEdgeIdx == hb1) {
		vertices[vb].halfEdgeIdx = ha2;  // ha2 now originates FROM vb
	}
	
	// Update signpost angle for ha1
	// The clockwise neighbor to ha1 is hb1.next (ha3)
	uint32_t ha1Neighbor = halfEdges[hb1].next; 
	if (ha1Neighbor != INVALID_INDEX) {
		halfEdges[ha1].signpostAngle = halfEdges[ha1Neighbor].signpostAngle + halfEdges[ha1Neighbor].cornerAngle;
	}

	// Update signpost angle for hb1
	// The clockwise neighbor to hb1 is ha1.next (hb3)
	uint32_t hb1Neighbor = halfEdges[ha1].next;
	if (hb1Neighbor != INVALID_INDEX) {
		halfEdges[hb1].signpostAngle = halfEdges[hb1Neighbor].signpostAngle + halfEdges[hb1Neighbor].cornerAngle;
	}

	/*
	// DEBUG
	std::cout << "[flipEdge] Signpost angles after flip:\n";
	std::cout << "  ha1 (he" << ha1 << " origin=" << halfEdges[ha1].origin << "): " << halfEdges[ha1].signpostAngle << "\n";
	std::cout << "  hb1 (he" << hb1 << " origin=" << halfEdges[hb1].origin << "): " << halfEdges[hb1].signpostAngle << "\n";

	if (ha1Neighbor != INVALID_INDEX) {
		std::cout << "  ha1Neighbor (he" << ha1Neighbor << "): angle=" << halfEdges[ha1Neighbor].signpostAngle
		          << " corner=" << halfEdges[ha1Neighbor].cornerAngle << "\n";
	}
	if (hb1Neighbor != INVALID_INDEX) {
		std::cout << "  hb1Neighbor (he" << hb1Neighbor << "): angle=" << halfEdges[hb1Neighbor].signpostAngle
		          << " corner=" << halfEdges[hb1Neighbor].cornerAngle << "\n";
	}
	*/

	// Set flipped edge as non-original
	edges[edgeIdx].isOriginal = false;
	// Set the new diagonal length
	edges[edgeIdx].intrinsicLength = newLength; 

	/*
	std::cout << "[flipEdge] Edge " << edgeIdx << " was flipped from (" << va << "->" << vb
		<< ") to (" << vc << "->" << vd << ")\n";
	std::cout << "[flipEdge] Edge " << edgeIdx << " still uses he:" << diagonalHE
		<< " (" << halfEdges[diagonalHE].origin << "->" << halfEdges[halfEdges[diagonalHE].next].origin << ")\n";
	std::cout << "[flipEdge] Edge " << edgeIdx << " is now Non-Original\n";


	std::cout << "[flipEdge] Flip succeeded on edge " << edgeIdx
		<< " (verts: " << va << "," << vb << "," << vc << "," << vd << ")\n";
	std::cout << "[flipEdge] Changed diagonal from (" << va << "->" << vb
		<< ") to (" << vc << "->" << vd << ")\n";
	*/

	return true;
}

bool HalfEdgeMesh::isDelaunayEdge(uint32_t heIdx) const {
	// Boundary edges are always delaunay
	if (heIdx >= halfEdges.size()) 
		return true;
	const HalfEdge& he = halfEdges[heIdx];
	if (he.opposite == INVALID_INDEX)
		return true;

	// Layout the quad around this halfedge
	auto quad = layoutDiamond(heIdx);
	const glm::dvec2& p0 = quad[0], & p1 = quad[1], & p2 = quad[2], & p3 = quad[3];

	// Precalculate squared norms
	double p0_sq = double(p0.x) * p0.x + double(p0.y) * p0.y;
	double p1_sq = double(p1.x) * p1.x + double(p1.y) * p1.y;
	double p2_sq = double(p2.x) * p2.x + double(p2.y) * p2.y;
	double p3_sq = double(p3.x) * p3.x + double(p3.y) * p3.y;

	// Calculate the 4x4 incircle determinant by cofactor expansion
	double det = 0.0;
	// Row p0.x
	det += p0.x * (
		p1.y * (p2_sq - p3_sq)
		- p2.y * (p1_sq - p3_sq)
		+ p3.y * (p1_sq - p2_sq)
		);
	// Row p0.y
	det -= p0.y * (
		p1.x * (p2_sq - p3_sq)
		- p2.x * (p1_sq - p3_sq)
		+ p3.x * (p1_sq - p2_sq)
		);
	// Row p0_sq
	det += p0_sq * (
		p1.x * (p2.y - p3.y)
		- p2.x * (p1.y - p3.y)
		+ p3.x * (p1.y - p2.y)
		);
	// Row constant
	det -= 1.0 * (
		p1.x * (p2.y * p3_sq - p3.y * p2_sq)
		- p2.x * (p1.y * p3_sq - p3.y * p1_sq)
		+ p3.x * (p1.y * p2_sq - p2.y * p1_sq)
		);

	// If det > 0, p3 is inside therefore not delaunay
	const double EPS = 1e-10;
	return det <= EPS;
}

int HalfEdgeMesh::makeDelaunay(int maxIterations, std::vector<uint32_t>* flippedEdges) {
	int totalFlips = 0;

	std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> flippedAB;

	for (int iter = 0; iter < maxIterations; ++iter) {
		std::queue<uint32_t> queueHE;
		std::unordered_set<uint32_t> inQueue;

		// 1) Enqueue all non-delaunay edges
		auto Es = getEdges();
		std::queue<uint32_t> queueEdges;
		std::unordered_set<uint32_t> inQueueEdges;
		
		for (uint32_t ei = 0; ei < Es.size(); ++ei) {
			uint32_t he = Es[ei].halfEdgeIdx;
			if (he != INVALID_INDEX && !isDelaunayEdge(he)) {
				queueEdges.push(ei);
				inQueueEdges.insert(ei);
			}
		}

		if (queueEdges.empty()) 
			break;

		// 2) Process 
		int flipsThisIter = 0;
		while (!queueEdges.empty()) {
			uint32_t edgeIdx = queueEdges.front();
			queueEdges.pop();
			inQueueEdges.erase(edgeIdx);

			if (edgeIdx >= edges.size()) 
				continue;
			
			uint32_t he = edges[edgeIdx].halfEdgeIdx;

			if (he >= halfEdges.size() || isDelaunayEdge(he))
				continue;

			// Avoid flipping the same undirected edge twice
			auto vA = halfEdges[he].origin;
			auto vB = halfEdges[halfEdges[he].next].origin;
			auto key = std::minmax(vA, vB);

			if (flippedAB.count(key)) 
				continue;

			if (flipEdge(edgeIdx)) { 
				++flipsThisIter;
				++totalFlips;
				flippedAB.insert(key);

				// Track which edge was flipped if requested
				if (flippedEdges) {
					flippedEdges->push_back(edgeIdx);
				}

				// Enqueue neighboring edges
				for (auto nhe : getNeighboringHalfEdges(he)) {
					uint32_t neighEdgeIdx = getEdgeFromHalfEdge(nhe);
					if (neighEdgeIdx != INVALID_INDEX && !inQueueEdges.count(neighEdgeIdx)) {
						queueEdges.push(neighEdgeIdx);
						inQueueEdges.insert(neighEdgeIdx);
					}
				}
			}
		}

		// 3) Finished if no more flips
		if (flipsThisIter == 0)
			break;

		// 4) Prepare for next iteration

		flippedAB.clear();
	}

	return totalFlips;
}

uint32_t HalfEdgeMesh::addIntrinsicVertex() {
	Vertex newVertex;
	newVertex.position = glm::vec3(0.0f);
	newVertex.halfEdgeIdx = INVALID_INDEX;
	newVertex.originalIndex = INVALID_INDEX; 

	vertices.push_back(newVertex);
	return static_cast<uint32_t>(vertices.size() - 1);
}

uint32_t HalfEdgeMesh::splitTriangleIntrinsic(uint32_t faceIdx, double r0, double r1, double r2) {
	if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX)
		return INVALID_INDEX;

	// Get the three halfedges of the triangle
	uint32_t he0 = faces[faceIdx].halfEdgeIdx;
	uint32_t he1 = halfEdges[he0].next;
	uint32_t he2 = halfEdges[he1].next;

	if (halfEdges[he2].next != he0)
		return INVALID_INDEX;

	// Get the three vertices
	uint32_t v0 = halfEdges[he0].origin;
	uint32_t v1 = halfEdges[he1].origin;
	uint32_t v2 = halfEdges[he2].origin;

	// Add the new intrinsic vertex
	uint32_t newV = addIntrinsicVertex();
	if (newV == INVALID_INDEX)
		return INVALID_INDEX;

	// Allocate 6 new halfedges
	uint32_t baseIdx = static_cast<uint32_t>(halfEdges.size());
	halfEdges.resize(baseIdx + 6);
	uint32_t newHe01 = baseIdx + 0;
	uint32_t newHe12 = baseIdx + 1;
	uint32_t newHe20 = baseIdx + 2;
	uint32_t newHe10 = baseIdx + 3;
	uint32_t newHe21 = baseIdx + 4;
	uint32_t newHe02 = baseIdx + 5;

	// Create 3 faces 
	uint32_t f1 = faceIdx;
	uint32_t f2 = faces.size();
	uint32_t f3 = f2 + 1;
	faces.resize(faces.size() + 2);

	// Triangle 1: (v0->v1->newV) 
	halfEdges[he0].next = newHe10;
	halfEdges[he0].prev = newHe01;
	halfEdges[he0].face = f1;
	halfEdges[newHe10].origin = v1;
	halfEdges[newHe10].next = newHe01;
	halfEdges[newHe10].prev = he0;
	halfEdges[newHe10].opposite = newHe12;
	halfEdges[newHe10].face = f1;

	halfEdges[newHe01].origin = newV;
	halfEdges[newHe01].next = he0;
	halfEdges[newHe01].prev = newHe10;
	halfEdges[newHe01].opposite = newHe02;
	halfEdges[newHe01].face = f1;

	// Triangle 2: (v1->v2->newV) 
	halfEdges[he1].next = newHe21;
	halfEdges[he1].prev = newHe12;
	halfEdges[he1].face = f2;
	halfEdges[newHe21].origin = v2;
	halfEdges[newHe21].next = newHe12;
	halfEdges[newHe21].prev = he1;
	halfEdges[newHe21].opposite = newHe20;
	halfEdges[newHe21].face = f2;

	halfEdges[newHe12].origin = newV;
	halfEdges[newHe12].next = he1;
	halfEdges[newHe12].prev = newHe21;
	halfEdges[newHe12].opposite = newHe10;
	halfEdges[newHe12].face = f2;

	// Triangle 3: (v2->v0->newV) 
	halfEdges[he2].next = newHe02;
	halfEdges[he2].prev = newHe20;
	halfEdges[he2].face = f3;
	halfEdges[newHe02].origin = v0;
	halfEdges[newHe02].next = newHe20;
	halfEdges[newHe02].prev = he2;
	halfEdges[newHe02].opposite = newHe01;
	halfEdges[newHe02].face = f3;

	halfEdges[newHe20].origin = newV;
	halfEdges[newHe20].next = he2;
	halfEdges[newHe20].prev = newHe02;
	halfEdges[newHe20].opposite = newHe21;
	halfEdges[newHe20].face = f3;

	// Opposite pairs
	halfEdges[newHe01].opposite = newHe02;
	halfEdges[newHe02].opposite = newHe01;
	halfEdges[newHe12].opposite = newHe10;
	halfEdges[newHe10].opposite = newHe12;
	halfEdges[newHe20].opposite = newHe21;
	halfEdges[newHe21].opposite = newHe20;

	// Create Edge entries
	uint32_t newEdge0 = edges.size();
	edges.emplace_back(newHe01);
	edges[newEdge0].intrinsicLength = r0;
	edges[newEdge0].isOriginal = false;
	halfEdges[newHe01].edgeIdx = newEdge0;
	halfEdges[newHe02].edgeIdx = newEdge0;

	uint32_t newEdge1 = edges.size();
	edges.emplace_back(newHe12);
	edges[newEdge1].intrinsicLength = r1;
	edges[newEdge1].isOriginal = false;
	halfEdges[newHe12].edgeIdx = newEdge1;
	halfEdges[newHe10].edgeIdx = newEdge1;

	uint32_t newEdge2 = edges.size();
	edges.emplace_back(newHe20);
	edges[newEdge2].intrinsicLength = r2;
	edges[newEdge2].isOriginal = false;
	halfEdges[newHe20].edgeIdx = newEdge2;
	halfEdges[newHe21].edgeIdx = newEdge2;

	// Update faces and vertex
	faces[f1].halfEdgeIdx = he0;
	faces[f2].halfEdgeIdx = he1;
	faces[f3].halfEdgeIdx = he2;
	vertices[newV].halfEdgeIdx = newHe01;  // outgoing halfedge from newV
	// Debug zero length check on the new edges
	for (auto edgeIdx : { newEdge0, newEdge1, newEdge2 }) {
		//if (edges[edgeIdx].intrinsicLength <= 0.0)
			//std::cerr << "[BUG] zero length edge" << edgeIdx << std::endl;
	}
	return newV;
}

uint32_t HalfEdgeMesh::insertVertexAlongEdge(uint32_t edgeIdx) {
	//std::cout << "[insertVertexAlongEdge] Starting with edgeIdx=" << edgeIdx << std::endl;

	if (edgeIdx >= edges.size()) {
		//std::cout << "[insertVertexAlongEdge] ERROR: edgeIdx " << edgeIdx << " >= edges.size() " << edges.size() << std::endl;
		return INVALID_INDEX;
	}

	// Fetch the two halfedges of this edge
	uint32_t heA = edges[edgeIdx].halfEdgeIdx;
	uint32_t heB = halfEdges[heA].opposite;

	//std::cout << "[insertVertexAlongEdge] Original edge halfedges: heA=" << heA << ", heB=" << heB << std::endl;

	if (heB == INVALID_INDEX) {
		// Boundary edge
		//std::cout << "[insertVertexAlongEdge] ERROR: Boundary edge (heB=INVALID)" << std::endl;
		return INVALID_INDEX;
	}

	// Faces on each side
	uint32_t fA = halfEdges[heA].face;
	uint32_t fB = halfEdges[heB].face;
	//std::cout << "[insertVertexAlongEdge] Faces: fA=" << fA << ", fB=" << fB << std::endl;
	// Original vertices
	uint32_t vOrigA = halfEdges[heA].origin;
	uint32_t vOrigB = halfEdges[heB].origin;

	// Create new vertex
	uint32_t newV = vertices.size();
	vertices.emplace_back();
	vertices[newV].halfEdgeIdx = heA;

	//std::cout << "[insertVertexAlongEdge] Created new vertex: newV=" << newV << std::endl;
	// Create new halfedge pair heAnew <-> heBnew
	uint32_t heAnew = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t heBnew = halfEdges.size();
	halfEdges.emplace_back();

	//std::cout << "[insertVertexAlongEdge] Created new halfedges: heAnew=" << heAnew << ", heBnew=" << heBnew << std::endl;

	// Mark opposites
	halfEdges[heAnew].opposite = heBnew;
	halfEdges[heBnew].opposite = heAnew;

	// Origin & face: heAnew lives in fA, heBnew in fB
	halfEdges[heAnew].origin = halfEdges[heA].origin;
	halfEdges[heAnew].face = fA;
	halfEdges[heBnew].origin = newV;
	halfEdges[heBnew].face = fB;

	//std::cout << "[insertVertexAlongEdge] Set origins: heAnew.origin=" << halfEdges[heAnew].origin
	//	<< ", heBnew.origin=" << halfEdges[heBnew].origin << std::endl;

	// Splice into face A: hePrevA -> heAnew -> heA
	uint32_t hePrevA = halfEdges[heA].prev;
	//std::cout << "[insertVertexAlongEdge] Face A splice: hePrevA=" << hePrevA << " -> heAnew=" << heAnew << " -> heA=" << heA << std::endl;
	halfEdges[hePrevA].next = heAnew;
	halfEdges[heAnew].prev = hePrevA;
	halfEdges[heAnew].next = heA;
	halfEdges[heA].prev = heAnew;

	// Splice into face B: heB -> heBnew -> heNextB (this is already correct)
	uint32_t heNextB = halfEdges[heB].next;
	//std::cout << "[insertVertexAlongEdge] Face B splice: heB=" << heB << " -> heBnew=" << heBnew << " -> heNextB=" << heNextB << std::endl;
	halfEdges[heB].next = heBnew;
	halfEdges[heBnew].prev = heB;
	halfEdges[heBnew].next = heNextB;
	halfEdges[heNextB].prev = heBnew;

	// Move the origin of heA to the new vertex
	halfEdges[heA].origin = newV;

	// Update vertex halfedge pointer (store outgoing halfedges like Geometry Central)
	vertices[newV].halfEdgeIdx = heA;  // heA now originates FROM newV
	vertices[vOrigA].halfEdgeIdx = heAnew;  // heAnew originates FROM vOrigA
	//std::cout << "[insertVertexAlongEdge] SUCCESS: Returning heAnew=" << heAnew << std::endl;
	return heAnew;
}

uint32_t HalfEdgeMesh::connectVertices(uint32_t heA, uint32_t heB) {
	//std::cout << "[connectVertices] Starting with heA=" << heA << ", heB=" << heB << std::endl;

	// Validate inputs
	if (heA >= halfEdges.size() || heB >= halfEdges.size()) {
		//std::cout << "[connectVertices] ERROR: Invalid halfedge indices" << std::endl;
		return INVALID_INDEX;
	}

	uint32_t vA = halfEdges[heA].origin;
	uint32_t vB = halfEdges[heB].origin;
	uint32_t faceOrig = halfEdges[heA].face;

	//std::cout << "[connectVertices] Connecting vertices: vA=" << vA << ", vB=" << vB << " in face=" << faceOrig << std::endl;

	// DEBUG: Print the original face before modification
	//std::cout << "[DEBUG] BEFORE - Face " << faceOrig << " vertices: ";
	uint32_t walkHE = faces[faceOrig].halfEdgeIdx;
	for (int i = 0; i < 10 && walkHE != INVALID_INDEX; i++) {
		walkHE = halfEdges[walkHE].next;
		if (walkHE == faces[faceOrig].halfEdgeIdx) 
			break;
	}

	// Create new halfedge pair diagA <-> diagB
	uint32_t diagA = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t diagB = halfEdges.size();
	halfEdges.emplace_back();

	//std::cout << "[connectVertices] Created diagonal halfedges: diagA=" << diagA << ", diagB=" << diagB << std::endl;

	halfEdges[diagA].opposite = diagB;
	halfEdges[diagB].opposite = diagA;

	// They split the face of heA (and heB)
	uint32_t fOld = halfEdges[heA].face;
	uint32_t fNew = faces.size();
	faces.emplace_back();

	//std::cout << "[connectVertices] Splitting face: fOld=" << fOld << " -> fNew=" << fNew << std::endl;

	// Set origins
	halfEdges[diagA].origin = halfEdges[heA].origin;
	halfEdges[diagB].origin = halfEdges[heB].origin;

	//std::cout << "[connectVertices] Set diagonal origins: diagA.origin=" << halfEdges[diagA].origin << ", diagB.origin=" << halfEdges[diagB].origin << std::endl;

	// Store the previous pointers before we modify them
	uint32_t heAprev = halfEdges[heA].prev;
	uint32_t heBprev = halfEdges[heB].prev;

	//std::cout << "[connectVertices] Previous pointers: heAprev=" << heAprev << ", heBprev=" << heBprev << std::endl;

	// DEBUG
	//std::cout << "[connectVertices] About to remap faces from heB=" << heB << " to heA=" << heA << std::endl;
	//std::cout << "[connectVertices] Current face assignments before remap:" << std::endl;
	uint32_t cursor = heB;
	int walkCount = 0;
	do {
		//std::cout << "[connectVertices]   he" << cursor << ": origin=" << halfEdges[cursor].origin << ", face=" << halfEdges[cursor].face << ", next=" << halfEdges[cursor].next << std::endl;
		cursor = halfEdges[cursor].next;
		walkCount++;
		if (walkCount > 10) break;
	} while (cursor != heA && cursor != INVALID_INDEX);

	// Remap faces: walk from heB to heA, marking fNew
	//std::cout << "[connectVertices] Remapping faces from heB to heA..." << std::endl;
	cursor = heB;
	walkCount = 0;
	do {
		//std::cout << "[connectVertices]   Setting he" << cursor << ".face = " << fNew << std::endl;
		halfEdges[cursor].face = fNew;
		cursor = halfEdges[cursor].next;
		walkCount++;
		if (walkCount > 10) {
			//std::cout << "[connectVertices] WARNING: Face walk exceeded 10 steps, breaking" << std::endl;
			break;
		}
	} while (cursor != heA);

	// Connect diagA: heAprev -> diagA -> heB
	//std::cout << "[connectVertices] Connecting diagA: " << heAprev << " -> " << diagA << " -> " << heB << std::endl;
	halfEdges[heAprev].next = diagA;
	halfEdges[diagA].prev = heAprev;
	halfEdges[diagA].next = heB;
	halfEdges[heB].prev = diagA;
	halfEdges[diagA].face = fOld;

	// Connect diagB: heBprev -> diagB -> heA
	//std::cout << "[connectVertices] Connecting diagB: " << heBprev << " -> " << diagB << " -> " << heA << std::endl;
	halfEdges[heBprev].next = diagB;
	halfEdges[diagB].prev = heBprev;
	halfEdges[diagB].next = heA;
	halfEdges[heA].prev = diagB;
	halfEdges[diagB].face = fNew;

	// Update face-to-halfedge anchors
	faces[fOld].halfEdgeIdx = diagA;
	faces[fNew].halfEdgeIdx = diagB;

	rebuildFaceConnectivity(fOld);
	rebuildFaceConnectivity(fNew);
	//std::cout << "[connectVertices] Updated face anchors: faces[" << fOld << "].halfEdgeIdx=" << diagA << ", faces[" << fNew << "].halfEdgeIdx=" << diagB << std::endl;

	//std::cout << "[connectVertices] SUCCESS: Returning diagA=" << diagA << std::endl;
	return diagA;
}

HalfEdgeMesh::Split HalfEdgeMesh::splitEdgeTopo(uint32_t edgeIdx, double t) {
	//std::cout << "[splitEdgeTopo] edgeIdx=" << edgeIdx << ", t=" << t << std::endl;
	if (edgeIdx >= edges.size()) {
		// std::cout << "[splitEdgeTopo] ERROR: edgeIdx " << edgeIdx << " >= edges.size() " << edges.size() << std::endl;
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	// Store original edge info
	uint32_t originalHE = edges[edgeIdx].halfEdgeIdx;
	double originalLength = edges[edgeIdx].intrinsicLength;
	bool wasOriginal = edges[edgeIdx].isOriginal;

	// std::cout << "[splitEdgeTopo] Original edge: halfedge=" << originalHE << ", length=" << originalLength << std::endl;

	// Get original vertices for debugging
	uint32_t vOrig1 = halfEdges[originalHE].origin;
	uint32_t vOrig2 = halfEdges[halfEdges[originalHE].next].origin;
	// std::cout << "[splitEdgeTopo] Original edge connects vertices: " << vOrig1 << " -> " << vOrig2 << std::endl;

	// Split the original edge into a quad on each side
	// std::cout << "[splitEdgeTopo] Inserting vertex along edge..." << std::endl;

	uint32_t heFront = insertVertexAlongEdge(edgeIdx);

	if (heFront == INVALID_INDEX) {
		//std::cout << "[splitEdgeTopo] ERROR: insertVertexAlongEdge failed" << std::endl;
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	uint32_t heBack = halfEdges[heFront].opposite;

	// Find the new vertex 
	uint32_t newV = halfEdges[halfEdges[heFront].next].origin;
	// std::cout << "[splitEdgeTopo] After vertex insertion: heFront=" << heFront << ", heBack=" << heBack << ", newV=" << newV << std::endl;
	// Draw the diagonal in each quad to form triangles
	// std::cout << "[splitEdgeTopo] Drawing diagonals to form triangles..." << std::endl;
	uint32_t heFromNewV = originalHE;
	uint32_t heToThirdVertex = halfEdges[halfEdges[heFromNewV].next].next;
	uint32_t diagFront = connectVertices(heFromNewV, heToThirdVertex);

	// Do the same for the back quad if it exists
	uint32_t diagBack = HalfEdgeMesh::INVALID_INDEX;
	if (heBack != INVALID_INDEX && halfEdges[heBack].face != INVALID_INDEX) {
		uint32_t heFromNewVBack = heBack;
		uint32_t heToThirdVertexBack = halfEdges[halfEdges[heFromNewVBack].next].next;
		diagBack = connectVertices(heFromNewVBack, heToThirdVertexBack);
	}

	// Set intrinsic lengths for the two child halfedges
	// std::cout << "[splitEdgeTopo] Setting intrinsic lengths..." << std::endl;
	// Calculate lengths based on split fraction
	double lengthA = t * originalLength;        // First part: vOrig1 -> newV
	double lengthB = (1.0 - t) * originalLength; // Second part: newV -> vOrig2

	// std::cout << "[splitEdgeTopo] Split fraction t=" << t << ", originalLength=" << originalLength << std::endl;
	// std::cout << "[splitEdgeTopo] lengthA (first part)=" << lengthA << ", lengthB (second part)=" << lengthB << std::endl;
	// Set the two halfedges of the edge split
	uint32_t child1 = heFront;  // vOrig1 -> newV (left child)
	uint32_t child2 = originalHE; // newV -> vOrig2  (right child)
	
	// Update Edge members
	edges[edgeIdx].halfEdgeIdx = child1;
	edges[edgeIdx].intrinsicLength = lengthA;
	halfEdges[child1].edgeIdx = edgeIdx;
	uint32_t child1Opp = halfEdges[child1].opposite;
	if (child1Opp != INVALID_INDEX) {
		halfEdges[child1Opp].edgeIdx = edgeIdx;
	}

	uint32_t newEdgeIdx = edges.size();
	edges.emplace_back(child2);
	edges[newEdgeIdx].intrinsicLength = lengthB;
	halfEdges[child2].edgeIdx = newEdgeIdx;
	uint32_t child2Opp = halfEdges[child2].opposite;
	if (child2Opp != INVALID_INDEX) {
		halfEdges[child2Opp].edgeIdx = newEdgeIdx;
	}

	// Split edges are not original
	edges[edgeIdx].isOriginal = false;
	edges[newEdgeIdx].isOriginal = false;
	//std::cout << "[splitEdgeTopo] Updated original edge " << edgeIdx << ": halfedge=" << child1 << ", length=" << lengthA << std::endl;
	//std::cout << "[splitEdgeTopo] Created new edge " << newEdgeIdx << ": halfedge=" << child2 << ", length=" << lengthB << std::endl;

	// Create Edge entries for the diagonals
	if (diagFront != INVALID_INDEX) {
		uint32_t diagFrontEdgeIdx = edges.size();
		edges.emplace_back(diagFront);
		edges[diagFrontEdgeIdx].intrinsicLength = 0.0;
		edges[diagFrontEdgeIdx].isOriginal = false;
		halfEdges[diagFront].edgeIdx = diagFrontEdgeIdx;
		uint32_t diagFrontOpp = halfEdges[diagFront].opposite;
		if (diagFrontOpp != INVALID_INDEX) {
			halfEdges[diagFrontOpp].edgeIdx = diagFrontEdgeIdx;
		}
		//std::cout << "[splitEdgeTopo] Created edge " << diagFrontEdgeIdx << " for diagFront=" << diagFront << std::endl;
	}

	if (diagBack != INVALID_INDEX) {
		uint32_t diagBackEdgeIdx = edges.size();
		edges.emplace_back(diagBack);
		edges[diagBackEdgeIdx].intrinsicLength = 0.0;
		edges[diagBackEdgeIdx].isOriginal = false;
		halfEdges[diagBack].edgeIdx = diagBackEdgeIdx;
		uint32_t diagBackOpp = halfEdges[diagBack].opposite;
		if (diagBackOpp != INVALID_INDEX) {
			halfEdges[diagBackOpp].edgeIdx = diagBackEdgeIdx;
		}
		//std::cout << "[splitEdgeTopo] Created edge " << diagBackEdgeIdx << " for diagBack=" << diagBack << std::endl;
	}

	// Update face anchors 
	if (diagFront != INVALID_INDEX) {
		uint32_t fFront = halfEdges[diagFront].face;
		if (fFront != INVALID_INDEX) faces[fFront].halfEdgeIdx = diagFront;
	}
	if (diagBack != INVALID_INDEX) {
		uint32_t fBack = halfEdges[diagBack].face;
		if (fBack != INVALID_INDEX) faces[fBack].halfEdgeIdx = diagBack;
	}

	return { newV, child1, child2, diagFront, diagBack };
}

void HalfEdgeMesh::removeVertex(uint32_t v) {
	if (v >= vertices.size()) {
		//std::cerr << "[removeVertex] Error: vertex index " << v << " out of range (0.." << vertices.size() - 1 << ")\n";
		return;
	}

	uint32_t last = static_cast<uint32_t>(vertices.size()) - 1;
	if (v != last) {
		//std::cerr << "[removeVertex] Error: can only remove the last vertex (" << last << "), not " << v << "\n";
		return;
	}

	auto hes = getVertexHalfEdges(v);
	if (!hes.empty()) {
		//std::cerr << "[removeVertex] Error: vertex " << v << " still has " << hes.size() << " incident halfedges\n";
		return;
	}
	vertices.pop_back();
}

std::vector<uint32_t> HalfEdgeMesh::getVertexHalfEdges(uint32_t vertexIdx) const {
	std::vector<uint32_t> fan;
	if (vertexIdx >= vertices.size())
		return fan;

	uint32_t startHE = vertices[vertexIdx].halfEdgeIdx;
	if (startHE == INVALID_INDEX)
		return fan;

	if (startHE >= halfEdges.size()) {
		//std::cerr << "[getVertexHalfEdges] ERROR: vertex " << vertexIdx << " has invalid halfEdgeIdx " << startHE << std::endl;
		return fan;
	}

	// Check if this is halfedge originates from vertex v
	if (halfEdges[startHE].origin != vertexIdx) {
		//std::cerr << "[getVertexHalfEdges] WARNING: he" << startHE << " is not a valid outgoing halfedge for vertex " << vertexIdx << std::endl;
		return fan;
	}

	uint32_t he = startHE;
	do {
		// Collect outgoing halfedges
		fan.push_back(he);

		// Use next->next->opposite traversal for outgoing halfedges 
		uint32_t next1 = halfEdges[he].next;
		if (next1 == INVALID_INDEX) 
			break;
		uint32_t next2 = halfEdges[next1].next;
		if (next2 == INVALID_INDEX) 
			break;
		uint32_t opp = halfEdges[next2].opposite;
		if (opp == INVALID_INDEX) {
			break; 
		}
		
		he = opp;

	} while (he != startHE);

	return fan;
}

std::vector<uint32_t> HalfEdgeMesh::getVertexFaces(uint32_t vertexIdx) const {
	std::vector<uint32_t> faceIndices;
	std::set<uint32_t> uniqueFaces; 

	if (vertexIdx >= vertices.size() || vertices[vertexIdx].halfEdgeIdx == INVALID_INDEX)
		return faceIndices;

	// Get all halfedges from this vertex
	std::vector<uint32_t> vertexHalfEdges = getVertexHalfEdges(vertexIdx);

	// For each halfedge, get its canonical face
	for (uint32_t heIdx : vertexHalfEdges) {
		uint32_t faceIdx = halfEdges[heIdx].face;
		if (faceIdx != INVALID_INDEX) {
			uniqueFaces.insert(faceIdx);
		}
	}

	// Convert set to vector
	faceIndices.assign(uniqueFaces.begin(), uniqueFaces.end());
	return faceIndices;
}

std::vector<uint32_t> HalfEdgeMesh::getFaceHalfEdges(uint32_t faceIdx) const {
	std::vector<uint32_t> edgeIndices;

	if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX)
		return edgeIndices;

	uint32_t startIdx = faces[faceIdx].halfEdgeIdx;
	uint32_t currentIdx = startIdx;

	do {
		edgeIndices.push_back(currentIdx);
		currentIdx = halfEdges[currentIdx].next;
	} while (currentIdx != startIdx);

	return edgeIndices;
}

std::vector<uint32_t> HalfEdgeMesh::getFaceVertices(uint32_t faceIdx) const {
	std::vector<uint32_t> vertexIndices;

	if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX)
		return vertexIndices;

	uint32_t startIdx = faces[faceIdx].halfEdgeIdx;
	uint32_t currentIdx = startIdx;
	int safetyCounter = 0;
	const int MAX_ITERATIONS = 100;

	do {
		if (currentIdx >= halfEdges.size()) {
			//std::cerr << "Invalid halfedge index " << currentIdx << " in getFaceVertices\n";
			break;
		}

		vertexIndices.push_back(halfEdges[currentIdx].origin);

		currentIdx = halfEdges[currentIdx].next;

		// Safety check to prevent infinite loops
		if (++safetyCounter > MAX_ITERATIONS) {
			//std::cerr << "Possible infinite loop in getFaceVertices for face " << faceIdx << "\n";
			break;
		}
	} while (currentIdx != startIdx && currentIdx != INVALID_INDEX);

	return vertexIndices;
}

std::vector<uint32_t> HalfEdgeMesh::getNeighboringHalfEdges(uint32_t heIdx) const {
	std::vector<uint32_t> out;
	if (heIdx >= halfEdges.size()) return out;

	// The two endpoints of the flipped halfedge:
	uint32_t vA = halfEdges[heIdx].origin;
	uint32_t vB = halfEdges[halfEdges[heIdx].next].origin;
	uint32_t opp = halfEdges[heIdx].opposite;

	// Collect halfedges from each vertex
	auto addFan = [&](uint32_t v) {
		// Get all halfedges originating from this vertex
		std::vector<uint32_t> vertexHEs = getVertexHalfEdges(v);

		for (auto h : vertexHEs) {
			if (h != heIdx && h != opp &&
				std::find(out.begin(), out.end(), h) == out.end())
			{
				out.push_back(h);
			}
		}
		};

	addFan(vA);
	addFan(vB);
	return out;
}

std::pair<uint32_t, uint32_t> HalfEdgeMesh::getEdgeVertices(uint32_t edgeIdx) const {
	if (edgeIdx >= edges.size()) {
		return { INVALID_INDEX, INVALID_INDEX };
	}

	uint32_t he = edges[edgeIdx].halfEdgeIdx;
	if (he >= halfEdges.size()) {
		return { INVALID_INDEX, INVALID_INDEX };
	}

	uint32_t v1 = halfEdges[he].origin;
	uint32_t v2 = INVALID_INDEX;

	// Get the other vertex from opposite halfedge
	uint32_t opposite = halfEdges[he].opposite;
	if (opposite != INVALID_INDEX && opposite < halfEdges.size()) {
		v2 = halfEdges[opposite].origin;
	}
	else {
		// Fallback for boundary edges: use next vertex in face
		uint32_t next = halfEdges[he].next;
		if (next != INVALID_INDEX && next < halfEdges.size()) {
			v2 = halfEdges[next].origin;
		}
	}

	return { v1, v2 };
}

uint32_t HalfEdgeMesh::getEdgeFromHalfEdge(uint32_t heIdx) const {
	if (heIdx == INVALID_INDEX || heIdx >= halfEdges.size())
		return INVALID_INDEX;

	return halfEdges[heIdx].edgeIdx;
}

uint32_t HalfEdgeMesh::findEdge(uint32_t v1Idx, uint32_t v2Idx) const {
	if (v1Idx >= vertices.size() || vertices[v1Idx].halfEdgeIdx == INVALID_INDEX)
		return INVALID_INDEX;

	uint32_t startIdx = vertices[v1Idx].halfEdgeIdx;
	uint32_t currentIdx = startIdx;
	uint32_t safety = 0;

	do {
		const HalfEdge& current = halfEdges[currentIdx];
		const HalfEdge& next = halfEdges[current.next];

		if (next.origin == v2Idx) {
			return currentIdx;
		}

		if (current.opposite == INVALID_INDEX)
			break;

		currentIdx = halfEdges[current.opposite].next;

		if (currentIdx == INVALID_INDEX)
			break;

		if (++safety > 1000)
			break;

	} while (currentIdx != startIdx);

	return INVALID_INDEX;
}

uint32_t HalfEdgeMesh::findFace(uint32_t e1Idx, uint32_t e2Idx) const {
	if (e1Idx == INVALID_INDEX || e2Idx == INVALID_INDEX)
		return INVALID_INDEX;

	uint32_t he1Idx = edges[e1Idx].halfEdgeIdx;
	uint32_t he2Idx = edges[e2Idx].halfEdgeIdx;

	const HalfEdge& he1 = halfEdges[he1Idx];
	const HalfEdge& he2 = halfEdges[he2Idx];

	// Check if both halfedges share the same face
	if (he1.face != INVALID_INDEX && he1.face == he2.face)
		return he1.face;

	// Check if the opposite halfedges share the same face
	if (he1.opposite != INVALID_INDEX && he2.opposite != INVALID_INDEX) {
		const HalfEdge& he1Opp = halfEdges[he1.opposite];
		const HalfEdge& he2Opp = halfEdges[he2.opposite];

		if (he1Opp.face != INVALID_INDEX && he1Opp.face == he2Opp.face)
			return he1Opp.face;
	}

	// No face found
	return INVALID_INDEX;
}

bool HalfEdgeMesh::isBoundaryVertex(uint32_t vertexIdx) const {
	if (vertexIdx >= vertices.size()) {
		return false;
	}

	// Get the first halfedge from the vertex
	uint32_t firstHalfEdge = vertices[vertexIdx].halfEdgeIdx;
	if (firstHalfEdge == INVALID_INDEX) {
		return false;
	}

	// Check if any halfedge around the vertex is a boundary halfedge
	uint32_t currentHalfEdge = firstHalfEdge;
	do {
		// If the halfedge has no opposite, its a boundary
		if (halfEdges[currentHalfEdge].opposite == INVALID_INDEX) {
			return true;
		}

		// Move to the next halfedge around the vertex
		currentHalfEdge = getNextAroundVertex(currentHalfEdge);

		// Boundary found if loop around ends
		if (currentHalfEdge == INVALID_INDEX) {
			return true;
		}
	} while (currentHalfEdge != firstHalfEdge);

	return false;
}

bool HalfEdgeMesh::isInteriorHalfEdge(uint32_t heIdx) const {
	if (heIdx >= halfEdges.size()) return false;
	return halfEdges[heIdx].opposite != INVALID_INDEX;
}

size_t HalfEdgeMesh::countBoundaryEdges() const {
	size_t c = 0;
	for (auto& e : edges) {
		if (halfEdges[e.halfEdgeIdx].opposite == INVALID_INDEX) {
			++c;
		}
	}
	return c;
}

std::vector<glm::vec3> HalfEdgeMesh::getVertexPositions() const {
	std::vector<glm::vec3> positions;
	positions.reserve(vertices.size());
	for (const auto& vertex : vertices) {
		positions.push_back(vertex.position);
	}
	return positions;
}

void HalfEdgeMesh::setVertexPositions(const std::vector<glm::vec3>& newPositions) {
	if (newPositions.size() != vertices.size()) {
		//std::cerr << "Warning: Position count mismatch" << std::endl;
		return;
	}
	for (size_t i = 0; i < newPositions.size(); ++i) {
		vertices[i].position = newPositions[i];
	}
}

void HalfEdgeMesh::debugPrintStats() const {
	std::cout << "HalfEdgeMesh Statistics:" << std::endl;
	std::cout << "  Vertices: " << vertices.size() << std::endl;
	std::cout << "  Edges: " << edges.size() << std::endl;
	std::cout << "  Faces: " << faces.size() << std::endl;
	std::cout << "  HalfEdges: " << halfEdges.size() << std::endl;
	std::cout << "  Is Manifold: " << (isManifold() ? "Yes" : "No") << std::endl;

	// Check for boundary edges
	int boundaryEdges = 0;
	for (const auto& edge : edges) {
		if (halfEdges[edge.halfEdgeIdx].opposite == INVALID_INDEX) {
			boundaryEdges++;
		}
	}
	std::cout << "  Boundary Edges: " << boundaryEdges << std::endl;
}