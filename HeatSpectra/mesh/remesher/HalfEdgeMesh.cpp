#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <utility> 
#include <algorithm>
#include <set>
#include <iostream>

#include "scene/Model.hpp"
#include "HalfEdgeMesh.hpp"

namespace {

void buildFromIndexedMesh(
	HalfEdgeMesh& mesh,
	const std::vector<glm::vec3>& vertexPositions,
	const std::vector<uint32_t>& indices) {
	auto& vertices = mesh.getVertices();
	auto& edges = mesh.getEdges();
	auto& faces = mesh.getFaces();
	auto& halfEdges = mesh.getHalfEdges();

	vertices.clear();
	edges.clear();
	faces.clear();
	halfEdges.clear();

	vertices.resize(vertexPositions.size());
	for (size_t i = 0; i < vertexPositions.size(); ++i) {
		vertices[i].position = vertexPositions[i];
		vertices[i].originalIndex = static_cast<uint32_t>(i);
		vertices[i].halfEdgeIdx = HalfEdgeMesh::INVALID_INDEX;
	}

	const size_t triangleCount = indices.size() / 3;
	halfEdges.reserve(triangleCount * 3);
	faces.reserve(triangleCount);

	std::unordered_map<std::pair<uint32_t, uint32_t>, uint32_t, HalfEdgeMesh::pair_hash> halfEdgeMap;

	for (size_t i = 0; i < triangleCount; ++i) {
		const uint32_t idx0 = indices[i * 3];
		const uint32_t idx1 = indices[i * 3 + 1];
		const uint32_t idx2 = indices[i * 3 + 2];

		if (idx0 >= vertices.size() || idx1 >= vertices.size() || idx2 >= vertices.size()) {
			continue;
		}
		if (idx0 == idx1 || idx1 == idx2 || idx2 == idx0) {
			continue;
		}

		HalfEdgeMesh::Face face;
		const uint32_t faceIdx = static_cast<uint32_t>(faces.size());
		const uint32_t he0Idx = static_cast<uint32_t>(halfEdges.size());
		const uint32_t he1Idx = he0Idx + 1;
		const uint32_t he2Idx = he0Idx + 2;

		HalfEdgeMesh::HalfEdge he0, he1, he2;
		he0.origin = idx0;
		he1.origin = idx1;
		he2.origin = idx2;
		he0.face = faceIdx;
		he1.face = faceIdx;
		he2.face = faceIdx;
		he0.next = he1Idx;
		he1.next = he2Idx;
		he2.next = he0Idx;
		he0.prev = he2Idx;
		he1.prev = he0Idx;
		he2.prev = he1Idx;
		face.halfEdgeIdx = he0Idx;

		halfEdgeMap[{idx0, idx1}] = he0Idx;
		halfEdgeMap[{idx1, idx2}] = he1Idx;
		halfEdgeMap[{idx2, idx0}] = he2Idx;

		vertices[idx0].halfEdgeIdx = he0Idx;
		vertices[idx1].halfEdgeIdx = he1Idx;
		vertices[idx2].halfEdgeIdx = he2Idx;

		halfEdges.push_back(he0);
		halfEdges.push_back(he1);
		halfEdges.push_back(he2);
		faces.push_back(face);
	}

	for (auto& pair : halfEdgeMap) {
		const uint32_t v1 = pair.first.first;
		const uint32_t v2 = pair.first.second;
		const uint32_t heIdx = pair.second;
		auto oppositeIt = halfEdgeMap.find({ v2, v1 });
		if (oppositeIt != halfEdgeMap.end()) {
			halfEdges[heIdx].opposite = oppositeIt->second;
		}
	}

	edges.reserve(halfEdges.size() / 2);
	std::vector<std::pair<uint32_t, uint32_t>> edgeOrder;
	std::set<std::pair<uint32_t, uint32_t>> seenEdges;
	for (size_t triangleIdx = 0; triangleIdx < indices.size() / 3; ++triangleIdx) {
		const uint32_t v0 = indices[triangleIdx * 3];
		const uint32_t v1 = indices[triangleIdx * 3 + 1];
		const uint32_t v2 = indices[triangleIdx * 3 + 2];
		const std::vector<std::pair<uint32_t, uint32_t>> triangleEdges = {
			{v0, v1}, {v1, v2}, {v2, v0}
		};
		for (const auto& directedEdge : triangleEdges) {
			const auto unorderedEdge = std::minmax(directedEdge.first, directedEdge.second);
			if (seenEdges.find(unorderedEdge) == seenEdges.end()) {
				seenEdges.insert(unorderedEdge);
				edgeOrder.push_back(directedEdge);
			}
		}
	}

	for (const auto& directedEdge : edgeOrder) {
		const uint32_t v1 = directedEdge.first;
		const uint32_t v2 = directedEdge.second;
		uint32_t foundHE = HalfEdgeMesh::INVALID_INDEX;
		auto it = halfEdgeMap.find({v1, v2});
		if (it != halfEdgeMap.end()) {
			foundHE = it->second;
		} else {
			auto reverseIt = halfEdgeMap.find({v2, v1});
			if (reverseIt != halfEdgeMap.end()) {
				foundHE = reverseIt->second;
			}
		}

		if (foundHE != HalfEdgeMesh::INVALID_INDEX) {
			const uint32_t newEdgeIdx = static_cast<uint32_t>(edges.size());
			edges.emplace_back(foundHE);
			halfEdges[foundHE].edgeIdx = newEdgeIdx;
			const uint32_t oppositeHE = halfEdges[foundHE].opposite;
			if (oppositeHE != HalfEdgeMesh::INVALID_INDEX) {
				halfEdges[oppositeHE].edgeIdx = newEdgeIdx;
			}
		}
	}
}

} // namespace

void HalfEdgeMesh::buildFromIndexedData(
	const std::vector<float>& pointPositions,
	const std::vector<uint32_t>& triangleIndices) {
	std::vector<glm::vec3> vertexPositions;
	vertexPositions.reserve(pointPositions.size() / 3);
	for (size_t index = 0; index + 2 < pointPositions.size(); index += 3) {
		vertexPositions.emplace_back(
			pointPositions[index],
			pointPositions[index + 1],
			pointPositions[index + 2]);
	}
	buildFromIndexedMesh(*this, vertexPositions, triangleIndices);

	if (!isManifold()) {
		std::cerr << "[HalfEdgeMesh] Indexed mesh is not manifold" << std::endl;
		return;
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
		if (heIdx >= halfEdges.size()) 
			continue;
		
		// Skip any bad HEs
		if (halfEdges[heIdx].next == INVALID_INDEX || halfEdges[heIdx].origin == INVALID_INDEX)
			continue;

		uint32_t v0 = halfEdges[heIdx].origin;
		uint32_t v1 = halfEdges[halfEdges[heIdx].next].origin;
		if (v1 == INVALID_INDEX) 
			continue;

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

	if (faceIdx >= faces.size()) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}

	// Get HEs of face
	uint32_t he0 = faces[faceIdx].halfEdgeIdx;
	if (he0 == INVALID_INDEX || he0 >= halfEdges.size()) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}

	uint32_t he1 = halfEdges[he0].next;
	if (he1 == INVALID_INDEX || he1 >= halfEdges.size()) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}
	uint32_t he2 = halfEdges[he1].next;
	if (he2 == INVALID_INDEX || he2 >= halfEdges.size() || halfEdges[he2].next != he0) {
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
	if (result.indices[0] >= vertices.size() ||
		result.indices[1] >= vertices.size() ||
		result.indices[2] >= vertices.size()) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}

	// Edge lengths
	const uint32_t e0 = getEdgeFromHalfEdge(he0);
	const uint32_t e1 = getEdgeFromHalfEdge(he1);
	const uint32_t e2 = getEdgeFromHalfEdge(he2);
	if (e0 == INVALID_INDEX || e0 >= edges.size() ||
		e1 == INVALID_INDEX || e1 >= edges.size() ||
		e2 == INVALID_INDEX || e2 >= edges.size()) {
		for (int i = 0; i < 3; ++i) {
			result.vertices[i] = glm::dvec2(0.0);
			result.indices[i] = INVALID_INDEX;
			result.edgeLengths[i] = 0.0;
		}
		return result;
	}
	result.edgeLengths[0] = edges[e0].intrinsicLength;
	result.edgeLengths[1] = edges[e1].intrinsicLength;
	result.edgeLengths[2] = edges[e2].intrinsicLength;

	// Validate
	const double MIN_LENGTH = 1e-12;
	if (result.edgeLengths[0] < MIN_LENGTH ||
		result.edgeLengths[1] < MIN_LENGTH ||
		result.edgeLengths[2] < MIN_LENGTH) {
		return result;
	}

	const double EPS = 1e-12;
	double a = result.edgeLengths[0], b = result.edgeLengths[1], c = result.edgeLengths[2];
	if (!(a + b > c + EPS && a + c > b + EPS && b + c > a + EPS)) {
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
	if (heIdx >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	uint32_t opp0 = HEs[heIdx].opposite;
	if (opp0 == INVALID_INDEX || opp0 >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	uint32_t fa = HEs[heIdx].face;
	uint32_t fb = HEs[opp0].face;

	if (fa == INVALID_INDEX || fb == INVALID_INDEX) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	// Get HEs and vertices
	uint32_t he0 = heIdx;
	uint32_t he1 = HEs[he0].next;
	if (he1 == INVALID_INDEX || he1 >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}
	uint32_t he2 = HEs[he1].next;
	if (he2 == INVALID_INDEX || he2 >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}
	uint32_t opp1 = HEs[opp0].next;
	if (opp1 == INVALID_INDEX || opp1 >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}
	uint32_t opp2 = HEs[opp1].next;
	if (opp2 == INVALID_INDEX || opp2 >= HEs.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	uint32_t va = HEs[he0].origin;
	uint32_t vb = HEs[opp0].origin;
	uint32_t vc = HEs[he2].origin;
	uint32_t vd = HEs[opp2].origin;

	Triangle2D triA = layoutTriangle(fa);
	Triangle2D triB = layoutTriangle(fb);

	if (triA.edgeLengths[0] == 0.0 || triB.edgeLengths[0] == 0.0) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}

	// Get intrinsic lengths for the diamond edges
	const uint32_t edgeDiag = getEdgeFromHalfEdge(he0);
	const uint32_t edgeVbVc = getEdgeFromHalfEdge(he1);
	const uint32_t edgeVcVa = getEdgeFromHalfEdge(he2);
	const uint32_t edgeVaVd = getEdgeFromHalfEdge(opp1);
	const uint32_t edgeVdVb = getEdgeFromHalfEdge(opp2);
	if (edgeDiag == INVALID_INDEX || edgeDiag >= edges.size() ||
		edgeVbVc == INVALID_INDEX || edgeVbVc >= edges.size() ||
		edgeVcVa == INVALID_INDEX || edgeVcVa >= edges.size() ||
		edgeVaVd == INVALID_INDEX || edgeVaVd >= edges.size() ||
		edgeVdVb == INVALID_INDEX || edgeVdVb >= edges.size()) {
		return { glm::dvec2(0), glm::dvec2(0), glm::dvec2(0), glm::dvec2(0) };
	}
	double diagLen = edges[edgeDiag].intrinsicLength;		// va-vb
	double len_vb_vc = edges[edgeVbVc].intrinsicLength;		// vb-vc
	double len_vc_va = edges[edgeVcVa].intrinsicLength;		// vc-va
	double len_va_vd = edges[edgeVaVd].intrinsicLength;	// va-vd
	double len_vd_vb = edges[edgeVdVb].intrinsicLength;	// vd-vb

	// Layout: place va at (0,0), vb at (diagLen, 0)
	glm::dvec2 p1(0.0, 0.0);          // va
	glm::dvec2 p2(diagLen, 0.0);      // vb

	// Calculate vc position using triangle (va, vb, vc)
	double x_vc = (diagLen * diagLen + len_vc_va * len_vc_va - len_vb_vc * len_vb_vc) / (2.0 * diagLen);
	double y2_vc = len_vc_va * len_vc_va - x_vc * x_vc;
	if (y2_vc < 0.0) {
		y2_vc = 0.0;
	}

	// Place above x-axis
	double y_vc = std::sqrt(y2_vc);   
	glm::dvec2 p3(x_vc, y_vc);        // vc

	// Calculate vd position using triangle (va, vb, vd)
	double x_vd = (diagLen * diagLen + len_va_vd * len_va_vd - len_vd_vb * len_vd_vb) / (2.0 * diagLen);
	double y2_vd = len_va_vd * len_va_vd - x_vd * x_vd;
	if (y2_vd < 0.0) {
		y2_vd = 0.0;
	}

	// Place below x-axis for diamond layout
	double y_vd = -std::sqrt(y2_vd);  
	glm::dvec2 p4(x_vd, y_vd);        

	return { p1, p2, p3, p4 };
}

HalfEdgeMesh::VertexRing2D HalfEdgeMesh::buildVertexRing2D(uint32_t vertexIdx) const {
	VertexRing2D ring;
	ring.centerVertexIdx = vertexIdx;

	// Get all outgoing HEs in CCW order
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
		if (heIdx >= halfEdges.size()) 
			continue;

		uint32_t v1Idx = halfEdges[heIdx].origin;

		// Get the destination vertex safely
		uint32_t nextHeIdx = halfEdges[heIdx].next;
		if (nextHeIdx >= halfEdges.size()) 
			continue;

		uint32_t v2Idx = halfEdges[nextHeIdx].origin;

		// Count outgoing edges between these vertices
		int connections = 0;
		uint32_t startIdx = vertices[v1Idx].halfEdgeIdx;
		uint32_t currentIdx = startIdx;
		int safetyCounter = 0;

		while (currentIdx != INVALID_INDEX && safetyCounter < MAX_ITERATIONS) {
			safetyCounter++;

			if (currentIdx >= halfEdges.size()) 
				break;

			const HalfEdge& current = halfEdges[currentIdx];

			uint32_t nextIdx = current.next;
			if (nextIdx >= halfEdges.size()) 
				break;

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

			if (currentIdx >= halfEdges.size())
				break;

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

			if (checkIdx >= halfEdges.size()) 
				break;

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
	if (edgeIdx >= edges.size()) {
		return false;
	}
	
	uint32_t diagonalHE = edges[edgeIdx].halfEdgeIdx;
	if (diagonalHE >= halfEdges.size()) {
		return false;
	}
	uint32_t diagonal2HE = halfEdges[diagonalHE].opposite;
	if (diagonal2HE == INVALID_INDEX) {
		return false;
	}

	// Get the HEs of both triangles 
	uint32_t ha1 = diagonalHE;
	uint32_t ha2 = halfEdges[ha1].next;
	uint32_t ha3 = halfEdges[ha2].next;
	if (ha3 >= halfEdges.size() || halfEdges[ha3].next != ha1) {
		return false;
	}
	uint32_t hb1 = diagonal2HE;
	uint32_t hb2 = halfEdges[hb1].next;
	uint32_t hb3 = halfEdges[hb2].next;
	if (hb3 >= halfEdges.size() || halfEdges[hb3].next != hb1) {
		return false;
	}

	// Manifold checks 
	if (halfEdges[hb1].opposite != ha1) {
		return false;
	}
	if (ha2 == hb1 || hb2 == ha1) {
		return false;
	}

	// Vertices and faces
	uint32_t va = halfEdges[ha1].origin;
	uint32_t vb = halfEdges[hb1].origin;
	uint32_t vc = halfEdges[ha3].origin;
	uint32_t vd = halfEdges[hb3].origin;

	// Prevent trivials
	if (va == vb || va == vc || va == vd || vb == vc || vb == vd || vc == vd) {
		return false;
	}

	uint32_t fa = halfEdges[ha1].face;
	uint32_t fb = halfEdges[hb1].face;

	// Layout the diamond using current intrinsic lengths 
	auto positions = layoutDiamond(diagonalHE);
	// Compute new edge length 
	double newLength = glm::distance(positions[2], positions[3]);

	// Check if new length is valid
	if (!std::isfinite(newLength) || newLength < 1e-10) {
		return false;
	}

	// Lengths of the boundary edges of the diamond
	double lenAC = getIntrinsicLengthFromHalfEdge(ha3);
	double lenCB = getIntrinsicLengthFromHalfEdge(ha2);
	double lenBD = getIntrinsicLengthFromHalfEdge(hb3);
	double lenDA = getIntrinsicLengthFromHalfEdge(hb2);
	// New diagonal length
	double lenCD = newLength;

	// Update corner angles
	halfEdges[ha1].cornerAngle = lawOfCosinesAngle(lenCD, lenCB, lenBD); // Corner at vc in face fa
	halfEdges[hb3].cornerAngle = lawOfCosinesAngle(lenBD, lenCD, lenCB); // Corner at vd in face fa
	halfEdges[ha2].cornerAngle = lawOfCosinesAngle(lenCB, lenBD, lenCD); // Corner at vb in face fa

	halfEdges[hb1].cornerAngle = lawOfCosinesAngle(lenCD, lenDA, lenAC); // Corner at vd in face fb
	halfEdges[ha3].cornerAngle = lawOfCosinesAngle(lenAC, lenCD, lenDA); // Corner at vc in face fb
	halfEdges[hb2].cornerAngle = lawOfCosinesAngle(lenDA, lenAC, lenCD); // Corner at va in face fb

	// Update face's HEs
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

	// Update HE origins
	halfEdges[ha1].origin = vc;
	halfEdges[hb1].origin = vd;

	// Update vertex anchors
	// vc and vd cant be invalidated by the flip since they keep their outgoing HEs
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

	// Set flipped edge as non-original
	edges[edgeIdx].isOriginal = false;
	// Set the new diagonal length
	edges[edgeIdx].intrinsicLength = newLength; 

	return true;
}

bool HalfEdgeMesh::isDelaunayEdge(uint32_t heIdx) const {
	// Boundary edges are always delaunay
	if (heIdx >= halfEdges.size()) 
		return true;
	const HalfEdge& he = halfEdges[heIdx];
	if (he.opposite == INVALID_INDEX)
		return true;

	// Layout the quad around this HE
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
	std::vector<uint32_t> allEdges;
	allEdges.reserve(edges.size());
	for (uint32_t edgeIdx = 0; edgeIdx < edges.size(); ++edgeIdx) {
		if (edges[edgeIdx].halfEdgeIdx != INVALID_INDEX) {
			allEdges.push_back(edgeIdx);
		}
	}
	return makeDelaunayLocal(maxIterations, allEdges, flippedEdges);
}

int HalfEdgeMesh::makeDelaunayLocal(int maxIterations, const std::vector<uint32_t>& seedEdges, std::vector<uint32_t>* flippedEdges) {
	int totalFlips = 0;

	std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> flippedAB;

	for (int iter = 0; iter < maxIterations; ++iter) {
		std::queue<uint32_t> queueEdges;
		std::unordered_set<uint32_t> inQueueEdges;
		
		for (uint32_t edgeIdx : seedEdges) {
			if (edgeIdx >= edges.size()) {
				continue;
			}

			uint32_t he = edges[edgeIdx].halfEdgeIdx;
			if (he != INVALID_INDEX && !isDelaunayEdge(he) && !inQueueEdges.count(edgeIdx)) {
				queueEdges.push(edgeIdx);
				inQueueEdges.insert(edgeIdx);
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
			const uint32_t heNext = halfEdges[he].next;
			if (heNext == INVALID_INDEX || heNext >= halfEdges.size()) {
				continue;
			}
			auto vB = halfEdges[heNext].origin;
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

	// Get the three HEs of the triangle
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

	// Allocate 6 new HEs
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
	vertices[newV].halfEdgeIdx = newHe01;  // outgoing HE from newV
	return newV;
}

uint32_t HalfEdgeMesh::insertVertexAlongEdge(uint32_t edgeIdx) {
	if (edgeIdx >= edges.size()) {
		return INVALID_INDEX;
	}

	// Fetch the two HE of this edge
	uint32_t heA = edges[edgeIdx].halfEdgeIdx;
	uint32_t heB = halfEdges[heA].opposite;

	if (heB == INVALID_INDEX) {
		return INVALID_INDEX;
	}

	// Faces on each side
	uint32_t fA = halfEdges[heA].face;
	uint32_t fB = halfEdges[heB].face;

	// Original vertices
	uint32_t vOrigA = halfEdges[heA].origin;

	// Create new vertex
	uint32_t newV = vertices.size();
	vertices.emplace_back();
	vertices[newV].halfEdgeIdx = heA;

	// Create new halfedge pair heAnew <-> heBnew
	uint32_t heAnew = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t heBnew = halfEdges.size();
	halfEdges.emplace_back();

	// Mark opposites
	halfEdges[heAnew].opposite = heBnew;
	halfEdges[heBnew].opposite = heAnew;

	// Origin & face: heAnew lives in fA, heBnew in fB
	halfEdges[heAnew].origin = halfEdges[heA].origin;
	halfEdges[heAnew].face = fA;
	halfEdges[heBnew].origin = newV;
	halfEdges[heBnew].face = fB;

	// Splice into face A: hePrevA -> heAnew -> heA
	uint32_t hePrevA = halfEdges[heA].prev;

	halfEdges[hePrevA].next = heAnew;
	halfEdges[heAnew].prev = hePrevA;
	halfEdges[heAnew].next = heA;
	halfEdges[heA].prev = heAnew;

	// Splice into face B: heB -> heBnew -> heNextB
	uint32_t heNextB = halfEdges[heB].next;

	halfEdges[heB].next = heBnew;
	halfEdges[heBnew].prev = heB;
	halfEdges[heBnew].next = heNextB;
	halfEdges[heNextB].prev = heBnew;

	// Move the origin of heA to the new vertex
	halfEdges[heA].origin = newV;

	// Update vertex HE pointer
	vertices[newV].halfEdgeIdx = heA;  // heA now originates FROM newV
	vertices[vOrigA].halfEdgeIdx = heAnew;  // heAnew originates FROM vOrigA

	return heAnew;
}

uint32_t HalfEdgeMesh::connectVertices(uint32_t heA, uint32_t heB) {
	// Validate inputs
	if (heA >= halfEdges.size() || heB >= halfEdges.size()) {
		return INVALID_INDEX;
	}

	uint32_t vA = halfEdges[heA].origin;
	uint32_t vB = halfEdges[heB].origin;

	// Create new halfedge pair diagA <-> diagB
	uint32_t diagA = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t diagB = halfEdges.size();
	halfEdges.emplace_back();

	halfEdges[diagA].opposite = diagB;
	halfEdges[diagB].opposite = diagA;

	// They split the face of heA (and heB)
	uint32_t fOld = halfEdges[heA].face;
	uint32_t fNew = faces.size();
	faces.emplace_back();

	// Set origins
	halfEdges[diagA].origin = halfEdges[heA].origin;
	halfEdges[diagB].origin = halfEdges[heB].origin;

	// Store the previous pointers before we modify them
	uint32_t heAprev = halfEdges[heA].prev;
	uint32_t heBprev = halfEdges[heB].prev;

	// Remap faces: walk from heB to heA, marking fNew
	uint32_t cursor = heB;
	for (size_t walkCount = 0; walkCount < halfEdges.size(); ++walkCount) {
		if (cursor == INVALID_INDEX || cursor >= halfEdges.size()) {
			break;
		}
		halfEdges[cursor].face = fNew;
		cursor = halfEdges[cursor].next;
		if (cursor == heA) {
			break;
		}
	}

	// Connect diagA: heAprev -> diagA -> heB
	halfEdges[heAprev].next = diagA;
	halfEdges[diagA].prev = heAprev;
	halfEdges[diagA].next = heB;
	halfEdges[heB].prev = diagA;
	halfEdges[diagA].face = fOld;

	// Connect diagB: heBprev -> diagB -> heA
	halfEdges[heBprev].next = diagB;
	halfEdges[diagB].prev = heBprev;
	halfEdges[diagB].next = heA;
	halfEdges[heA].prev = diagB;
	halfEdges[diagB].face = fNew;

	// Update face-to-HE anchors
	faces[fOld].halfEdgeIdx = diagA;
	faces[fNew].halfEdgeIdx = diagB;

	rebuildFaceConnectivity(fOld);
	rebuildFaceConnectivity(fNew);

	return diagA;
}

HalfEdgeMesh::Split HalfEdgeMesh::splitEdgeTopo(uint32_t edgeIdx, double t) {
	if (edgeIdx >= edges.size()) {
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	// Store original edge info
	uint32_t originalHE = edges[edgeIdx].halfEdgeIdx;
	double originalLength = edges[edgeIdx].intrinsicLength;
	// Split the original edge into a quad on each side
	uint32_t heFront = insertVertexAlongEdge(edgeIdx);

	if (heFront == INVALID_INDEX) {
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	uint32_t heBack = halfEdges[heFront].opposite;

	// Find the new vertex 
	uint32_t newV = halfEdges[halfEdges[heFront].next].origin;

	// Draw the diagonal in each quad to form triangles
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

	// Calculate lengths based on split fraction
	double lengthA = t * originalLength;         // First segment of split edge
	double lengthB = (1.0 - t) * originalLength; // Second segment of split edge

	// Set the two HEs of the edge split
	uint32_t child1 = heFront;    // Left child HE
	uint32_t child2 = originalHE; // Right child HE

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

double HalfEdgeMesh::lawOfCosinesAngle(double a, double b, double opposite) const {
	if (a < 1e-12 || b < 1e-12) {
		return 0.0;
	}
	double q = (a * a + b * b - opposite * opposite) / (2.0 * a * b);
	q = std::max(-1.0, std::min(1.0, q));
	return std::acos(q);
}

void HalfEdgeMesh::addNeighboringHalfEdgesFromVertex(uint32_t vertexIdx, uint32_t excludedA, uint32_t excludedB, std::vector<uint32_t>& out) const {
	std::vector<uint32_t> vertexHEs = getVertexHalfEdges(vertexIdx);
	for (uint32_t h : vertexHEs) {
		if (h == excludedA || h == excludedB) {
			continue;
		}
		if (std::find(out.begin(), out.end(), h) == out.end()) {
			out.push_back(h);
		}
	}
}

uint32_t HalfEdgeMesh::getNextAroundVertex(uint32_t halfEdgeIdx) const {
	// Standardized CCW traversal around a vertex
	if (halfEdgeIdx == INVALID_INDEX || halfEdgeIdx >= halfEdges.size()) {
		return INVALID_INDEX;
	}
	uint32_t next1 = halfEdges[halfEdgeIdx].next;
	if (next1 == INVALID_INDEX || next1 >= halfEdges.size()) {
		return INVALID_INDEX;
	}
	uint32_t next2 = halfEdges[next1].next;
	if (next2 == INVALID_INDEX || next2 >= halfEdges.size()) {
		return INVALID_INDEX;
	}
	uint32_t opp = halfEdges[next2].opposite;
	if (opp == INVALID_INDEX || opp >= halfEdges.size()) {
		return INVALID_INDEX;
	}
	return opp;
}

std::vector<uint32_t> HalfEdgeMesh::getVertexHalfEdges(uint32_t vertexIdx) const {
	std::vector<uint32_t> fan;
	if (vertexIdx >= vertices.size()) {
		return fan;
	}

	uint32_t startHE = vertices[vertexIdx].halfEdgeIdx;
	if (startHE == INVALID_INDEX || startHE >= halfEdges.size()) {
		return fan;
	}

	if (halfEdges[startHE].origin != vertexIdx) {
		return fan;
	}

	uint32_t he = startHE;
	const size_t maxSteps = std::max<size_t>(halfEdges.size(), 1);
	size_t steps = 0;
	do {
		fan.push_back(he);

		uint32_t nextHe = getNextAroundVertex(he);
		if (nextHe == INVALID_INDEX) {
			break;
		}

		he = nextHe;
		if (++steps > maxSteps) {
			break;
		}
	} while (he != startHE);

	return fan;
}

std::vector<uint32_t> HalfEdgeMesh::getVertexFaces(uint32_t vertexIdx) const {
	std::vector<uint32_t> faceIndices;
	std::set<uint32_t> uniqueFaces; 

	if (vertexIdx >= vertices.size() || vertices[vertexIdx].halfEdgeIdx == INVALID_INDEX)
		return faceIndices;

	// Get all HEs from this vertex
	std::vector<uint32_t> vertexHalfEdges = getVertexHalfEdges(vertexIdx);

	// For each HE, get its canonical face
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
	int safetyCounter = 0;
	const int MAX_ITERATIONS = 100;

	do {
		if (currentIdx == INVALID_INDEX || currentIdx >= halfEdges.size()) {
			break;
		}

		edgeIndices.push_back(currentIdx);
		currentIdx = halfEdges[currentIdx].next;

		if (++safetyCounter > MAX_ITERATIONS) {
			break;
		}
	} while (currentIdx != startIdx);

	return edgeIndices;
}

std::vector<uint32_t> HalfEdgeMesh::getFaceVertices(uint32_t faceIdx) const {
	std::vector<uint32_t> vertexIndices;
	auto faceHalfEdges = getFaceHalfEdges(faceIdx);
	vertexIndices.reserve(faceHalfEdges.size());
	for (uint32_t heIdx : faceHalfEdges) {
		if (heIdx >= halfEdges.size()) {
			break;
		}
		vertexIndices.push_back(halfEdges[heIdx].origin);
	}

	return vertexIndices;
}

std::vector<uint32_t> HalfEdgeMesh::getNeighboringHalfEdges(uint32_t heIdx) const {
	std::vector<uint32_t> out;
	if (heIdx >= halfEdges.size()) return out;

	// The two endpoints of the flipped halfedge
	uint32_t vA = halfEdges[heIdx].origin;
	uint32_t vB = halfEdges[halfEdges[heIdx].next].origin;
	uint32_t opp = halfEdges[heIdx].opposite;

	// Collect HEs from each vertex
	addNeighboringHalfEdgesFromVertex(vA, heIdx, opp, out);
	addNeighboringHalfEdgesFromVertex(vB, heIdx, opp, out);
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

	// Get the other vertex from opposite HE
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

double HalfEdgeMesh::getIntrinsicLengthFromHalfEdge(uint32_t halfEdgeIdx) const {
	uint32_t edgeIdx = getEdgeFromHalfEdge(halfEdgeIdx);
	if (edgeIdx == INVALID_INDEX || edgeIdx >= edges.size()) {
		return 0.0;
	}
	return edges[edgeIdx].intrinsicLength;
}

bool HalfEdgeMesh::isBoundaryVertex(uint32_t vertexIdx) const {
	if (vertexIdx >= vertices.size()) {
		return false;
	}

	// Get the first HE from the vertex
	uint32_t firstHalfEdge = vertices[vertexIdx].halfEdgeIdx;
	if (firstHalfEdge == INVALID_INDEX) {
		return false;
	}

	// Check if any HE around the vertex is a boundary HE
	uint32_t currentHalfEdge = firstHalfEdge;
	do {
		// If the HE has no opposite, its a boundary
		if (halfEdges[currentHalfEdge].opposite == INVALID_INDEX) {
			return true;
		}

		// Move to the next HE around the vertex
		currentHalfEdge = getNextAroundVertex(currentHalfEdge);

		// Boundary found if loop around ends
		if (currentHalfEdge == INVALID_INDEX) {
			return true;
		}
	} while (currentHalfEdge != firstHalfEdge);

	return false;
}

bool HalfEdgeMesh::isInteriorHalfEdge(uint32_t heIdx) const {
	if (heIdx >= halfEdges.size()) 
		return false;

	return halfEdges[heIdx].opposite != INVALID_INDEX;
}
