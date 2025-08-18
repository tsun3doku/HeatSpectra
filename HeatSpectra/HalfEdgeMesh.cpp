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

		// Check for degenerate triangles (triangles with duplicate vertices)
		if (idx0 == idx1 || idx1 == idx2 || idx2 == idx0) {
#ifndef NDEBUG
			std::cerr << "Skipping degenerate triangle " << i << " with vertices ["
				<< idx0 << ", " << idx1 << ", " << idx2 << "]\n";
#endif
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
		vertices[idx0].halfEdgeIdx = he0Idx;
		vertices[idx1].halfEdgeIdx = he1Idx;
		vertices[idx2].halfEdgeIdx = he2Idx;

		// Add the half-edges and face to our data structures
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

	// Create edges
	edges.reserve(halfEdges.size() / 2);
	for (auto& pair : halfEdgeMap) {
		uint32_t heIdx = pair.second;
		HalfEdge& he = halfEdges[heIdx];

		if (he.opposite == INVALID_INDEX) {
			// Boundary edge
			edges.emplace_back(heIdx);
		}
		else if (heIdx < he.opposite) {
			// Internal edge (add only once)
			edges.emplace_back(heIdx);
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
	// 1) Per�halfedge intrinsic length
	for (uint32_t heIdx = 0; heIdx < halfEdges.size(); ++heIdx) {
		HalfEdge& he = halfEdges[heIdx];
		// Skip any malformed halfedges
		if (he.next == INVALID_INDEX || he.origin == INVALID_INDEX)
			continue;

		uint32_t v0 = he.origin;
		uint32_t v1 = halfEdges[he.next].origin;
		if (v1 == INVALID_INDEX)
			continue;

		const glm::vec3& p0 = vertices[v0].position;
		const glm::vec3& p1 = vertices[v1].position;

		// Use double precision for the calculation
		glm::dvec3 dp0(p0.x, p0.y, p0.z);
		glm::dvec3 dp1(p1.x, p1.y, p1.z);
		double length = glm::length(dp1 - dp0);
		he.intrinsicLength = length;

		// DEBUG may remove soon
		if (length <= 0.0) {
#ifndef NDEBUG
#ifndef NDEBUG
			std::cerr << "[initializeIntrinsicLengths] BUG: intrinsicLength zero or negative at he"
#endif
				<< heIdx << " on edge ("
				<< he.origin << "->"
				<< v1
				<< ")\n";
#endif
		}
	}

	// 2) Mirror into Edge records
	for (auto& e : edges) {
		// Each Edge stores one representative halfEdgeIdx
		e.intrinsicLength = halfEdges[e.halfEdgeIdx].intrinsicLength;
	}
}

void HalfEdgeMesh::rebuildEdges() {
	edges.clear();
	std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> edgeSet;
	std::unordered_map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>, pair_hash> sharedMap;

	for (uint32_t heIdx = 0; heIdx < halfEdges.size(); ++heIdx) {
		const HalfEdge& he = halfEdges[heIdx];

		if (he.next == INVALID_INDEX || he.origin == INVALID_INDEX)
			continue;

		uint32_t v1 = he.origin;
		uint32_t v2 = halfEdges[he.next].origin;

		if (v2 == INVALID_INDEX) continue;

		auto e = std::minmax(v1, v2);

		if (edgeSet.insert(e).second) {
			edges.emplace_back(heIdx);
		}
		else {
			sharedMap[e].push_back(heIdx);
		}
	}
}

void HalfEdgeMesh::rebuildConnectivity() {
	// 1) Clear out all vertex and face anchors
	for (auto& V : vertices) {
		V.halfEdgeIdx = INVALID_INDEX;
	}
	for (auto& F : faces) {
		F.halfEdgeIdx = INVALID_INDEX;
	}

	// 2) Reset all prev pointers on halfedges
	for (auto& HE : halfEdges) {
		HE.prev = INVALID_INDEX;
	}

	// 3) Recompute prev <- next
	for (uint32_t i = 0; i < halfEdges.size(); ++i) {
		uint32_t ni = halfEdges[i].next;
		if (ni < halfEdges.size()) {
			halfEdges[ni].prev = i;
		}
	}

	// 4) Re anchor each vertex and face to one of its incident halfedges
	for (uint32_t i = 0; i < halfEdges.size(); ++i) {
		const auto& HE = halfEdges[i];

		// vertex anchor
		if (HE.origin < vertices.size() &&
			vertices[HE.origin].halfEdgeIdx == INVALID_INDEX)
		{
			vertices[HE.origin].halfEdgeIdx = i;
		}

		// face anchor
		if (HE.face < faces.size() &&
			faces[HE.face].halfEdgeIdx == INVALID_INDEX)
		{
			faces[HE.face].halfEdgeIdx = i;
		}
	}

	// 5) Rebuild halfedge to face mapping
	for (auto& HE : halfEdges) {
		HE.face = INVALID_INDEX;
	}

	// Walk the halfedges of each face and update the face members
	for (uint32_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
		if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) continue;

		uint32_t startHE = faces[faceIdx].halfEdgeIdx;
		uint32_t currentHE = startHE;

		do {
			halfEdges[currentHE].face = faceIdx;
			currentHE = halfEdges[currentHE].next;
		} while (currentHE != startHE && currentHE != INVALID_INDEX);
	}
}

void HalfEdgeMesh::rebuildFaceConnectivity(uint32_t faceIdx) {
	if (faceIdx == INVALID_INDEX) return;
	uint32_t start = faces[faceIdx].halfEdgeIdx;
	if (start == INVALID_INDEX) return;
	uint32_t cur = start;
	do {
		halfEdges[cur].face = faceIdx;
		cur = halfEdges[cur].next;
	} while (cur != start && cur != INVALID_INDEX);
}

void HalfEdgeMesh::rebuildOpposites() {
	// Map each directed edge (origin->dest) to its halfedge index
	std::unordered_map<std::pair<uint32_t, uint32_t>, uint32_t, pair_hash> edgeMap;
	edgeMap.reserve(halfEdges.size());
	for (uint32_t h = 0; h < halfEdges.size(); ++h) {
		uint32_t a = halfEdges[h].origin;
		uint32_t b = halfEdges[halfEdges[h].next].origin;
		edgeMap[{a, b}] = h;
	}
	// For each halfedge, find its opposite
	for (auto& he : halfEdges) {
		uint32_t a = he.origin;
		uint32_t b = halfEdges[he.next].origin;
		auto it = edgeMap.find({ b,a });
		he.opposite = (it != edgeMap.end() ? it->second : INVALID_INDEX);
	}
}

void HalfEdgeMesh::updateIntrinsicLength(uint32_t heIdx) {
	if (heIdx >= halfEdges.size()) return;

	// Find the two vertices of this halfedge
	uint32_t a = halfEdges[heIdx].origin;
	uint32_t heNext = halfEdges[heIdx].next;
	if (heNext >= halfEdges.size()) return;
	uint32_t b = halfEdges[heNext].origin;

	// Make sure both vertices exist
	if (a >= vertices.size() || b >= vertices.size()) return;

	// Recalculate the halfedges intrinsic length
	halfEdges[heIdx].intrinsicLength =
		glm::distance(vertices[a].position,
			vertices[b].position);

	// Copy into Edge�s length
	for (auto& e : edges) {
		if (e.halfEdgeIdx < halfEdges.size())
			e.intrinsicLength = halfEdges[e.halfEdgeIdx].intrinsicLength;
	}
}

std::array<glm::dvec2, 4> HalfEdgeMesh::layoutDiamond(uint32_t heIdx) const {
	const auto& HEs = halfEdges;
	// Find the six halfedges around the two triangles
	uint32_t opp0 = HEs[heIdx].opposite;
	uint32_t he0 = heIdx;
	uint32_t he1 = HEs[he0].next;
	uint32_t he2 = HEs[he1].next;
	uint32_t opp1 = HEs[opp0].next;
	uint32_t opp2 = HEs[opp1].next;

	// Get the five intrinsic lengths a-e
	double a = HEs[he0].intrinsicLength;   // diagonal
	double b = HEs[he1].intrinsicLength;   // rim1
	double c = HEs[he2].intrinsicLength;   // rim2
	double d = HEs[opp1].intrinsicLength;  // rim3
	double e = HEs[opp2].intrinsicLength;  // rim4

	// Get the vertex indices for each of those halfedges
	uint32_t v0 = HEs[he0].origin;
	uint32_t v1 = HEs[he1].origin;
	uint32_t v2 = HEs[he2].origin;
	uint32_t v3 = HEs[opp1].origin;
	uint32_t v4 = HEs[opp2].origin;

	// Origin vertices for each halfedge
	auto origin = [&](uint32_t he) { return HEs[he].origin; };
	auto dest = [&](uint32_t he) { return HEs[HEs[he].next].origin; };

	// DEBUG
	/*
#ifndef NDEBUG
	std::cout << "[layoutDiamond] he=" << heIdx << "\n"
		<< "   a (diag)  he0=" << he0
		<< "  verts=(" << origin(he0) << "-" << dest(he0) << ") len=" << a << "\n"
		<< "   b (rim1)  he1=" << he1
		<< "  verts=(" << origin(he1) << "-" << dest(he1) << ") len=" << b << "\n"
		<< "   c (rim2)  he2=" << he2
		<< "  verts=(" << origin(he2) << "-" << dest(he2) << ") len=" << c << "\n"
		<< "   d (rim3)  opp1=" << opp1
		<< "  verts=(" << origin(opp1) << "-" << dest(opp1) << ") len=" << d << "\n"
		<< "   e (rim4)  opp2=" << opp2
		<< "  verts=(" << origin(opp2) << "-" << dest(opp2) << ") len=" << e << "\n";
#endif
	*/

	glm::dvec2 p1(0.0, 0.0), p2(a, 0.0), p3, p4;

	// pure law of cosines layout, no clamp
	auto layoutTri = [&](glm::dvec2 A, glm::dvec2 B,
		double lenBC, double lenCA,
		int dbgHE) -> glm::dvec2
		{
			glm::dvec2 AB = B - A;
			double   dist = glm::length(AB);
			glm::dvec2 dir = AB / dist;

			// law of cosines:
			double x = (dist * dist + lenCA * lenCA - lenBC * lenBC) / (2.0 * dist);
			double y2 = lenCA * lenCA - x * x;

			if (y2 < 0.0) {
#ifndef NDEBUG
				std::cerr << "[layoutDiamond][ERROR] he=" << dbgHE
					<< " y2=" << y2
					<< " (x=" << x
					<< ", lenCA=" << lenCA
					<< ", lenBC=" << lenBC
					<< ", dist=" << dist
					<< ")\n";
#endif
			}

			double y = std::sqrt(std::max(y2, 0.0)); 
			return A + dir * x + glm::dvec2(-dir.y, dir.x) * y;
		};

	// embed both triangles
	p3 = layoutTri(p1, p2, c, b, heIdx);
	p4 = layoutTri(p1, p2, d, e, heIdx);
	p4.y = -p4.y;  // flip second triangle under the diagonal

	// compute areas
	auto area2D = [&](const glm::dvec2& A, const glm::dvec2& B, const glm::dvec2& C) {
			return std::abs((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x)) * 0.5;
		};

	double area1 = area2D(p1, p2, p3);
	double area2 = area2D(p1, p2, p4);
	const double MIN_AREA = 1e-16;

	if (area1 < MIN_AREA) {
#ifndef NDEBUG
		std::cout << "[layoutDiamond] Near-zero area1 (" << area1 << ") he=" << heIdx << "\n";
#endif
	}
	if (area2 < MIN_AREA) {
#ifndef NDEBUG
		std::cout << "[layoutDiamond] Near-zero area2 (" << area2 << ") he=" << heIdx
			<< " at p4=(" << p4.x << "," << p4.y << ")\n";
#endif
	}

	// DEBUG
	/*
#ifndef NDEBUG
	std::cout << "[layoutDiamond] 2D coords:\n"

		<< "   p1 (vertex " << origin(he0) << ") = (" << p1.x << "," << p1.y << ")\n"
		<< "   p2 (vertex " << dest(he0) << ") = (" << p2.x << "," << p2.y << ")\n"
		<< "   p3 (vertex " << dest(he1) << ") = (" << p3.x << "," << p3.y << ")\n"
		<< "   p4 (vertex " << dest(opp1) << ") = (" << p4.x << "," << p4.y << ")\n";
#endif
	*/

	return { p1, p2, p3, p4 };
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
				return false;  // Multiple components

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

bool HalfEdgeMesh::flipEdge(uint32_t diagonalHE) {
	// Define cross2d lambda
	auto cross2d = [](glm::dvec2 A, glm::dvec2 B) { return A.x * B.y - A.y * B.x; };

	// 1) Quick index check
	if (diagonalHE >= halfEdges.size()) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Invalid halfedge index: " << diagonalHE << "\n";
#endif
		return false;
	}

	// 2) Boundary check
	HalfEdge& diagonal1 = halfEdges[diagonalHE];
	uint32_t diagonal2HE = diagonal1.opposite;
	if (diagonal2HE == INVALID_INDEX) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Boundary edge (no opposite): " << diagonalHE << "\n";
#endif
		return false;
	}
	HalfEdge& diagonal2 = halfEdges[diagonal2HE];

	// Validate both triangle faces
	uint32_t f0 = diagonal1.face, f1 = diagonal2.face;
	if (f0 == INVALID_INDEX || f1 == INVALID_INDEX) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Invalid face reference\n";
#endif
		return false;
	}

	// 3) Set the four internal halfedges (the opposite of these are the rim halfedges)
	uint32_t internal1HE = diagonal1.next;				// v1->v2
	uint32_t internal2HE = halfEdges[internal1HE].next;	// v2->v0
	uint32_t internal3HE = diagonal2.next;				// v0->v3
	uint32_t internal4HE = halfEdges[internal3HE].next;	// v3->v1

	// Validate triangle structure
	if (halfEdges[internal2HE].next != diagonalHE || halfEdges[internal4HE].next != diagonal2HE) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Invalid face structure\n";
#endif
		return false;
	}

	// 4) Quad vertices
	uint32_t v0 = diagonal1.origin;				  // Original diagonal startpoint
	uint32_t v1 = halfEdges[internal1HE].origin;  // Original diagonal endpoint
	uint32_t v2 = halfEdges[internal2HE].origin;  // Third vertex of face f0
	uint32_t v3 = halfEdges[internal4HE].origin;  // Third vertex of face f1

	// 4b) Check vertex anchor consistency
	//std::cout << "\n Vertex anchor check" << std::endl;
	for (uint32_t v : {v0, v1, v2, v3}) {
		uint32_t anchor = vertices[v].halfEdgeIdx;
		//std::cout << "Vertex " << v << " anchor: he" << anchor;

		if (anchor == INVALID_INDEX) {
			std::cout << " (INVALID)" << std::endl;
		}
		else if (anchor >= halfEdges.size()) {
			std::cout << " (OUT OF BOUNDS)" << std::endl;
		}
		else {
			uint32_t anchorOrigin = halfEdges[anchor].origin;
			//std::cout << " (origin=" << anchorOrigin << ")";
			if (anchorOrigin != v) {
				std::cout << " ERROR: Anchor origin mismatch" << std::endl;;
			}
		}
	}

	if (v0 == v1 || v0 == v2 || v0 == v3 || v1 == v2 || v1 == v3 || v2 == v3) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Duplicate vertices in quad ["
			<< v0 << "," << v1 << "," << v2 << "," << v3 << "], skipping\n";
#endif
		return false;
	}

	// 5) Layout the diamond 
	auto quad = layoutDiamond(diagonalHE);
	const glm::dvec2& p0 = quad[0], & p1 = quad[1], & p2 = quad[2], & p3 = quad[3];

	// 1) Define area2d right here ---------------- MAY REMOVE THIS
	auto area2d = [](const glm::dvec2& A, const glm::dvec2& B, const glm::dvec2& C) {
		double ux = B.x - A.x;
		double uy = B.y - A.y;
		double vx = C.x - A.x;
		double vy = C.y - A.y;
		return std::abs(ux * vy - uy * vx) * 0.5;
		};

	// 2) Compute both triangle areas
	double a0 = area2d(p0, p1, p2);
	double a1 = area2d(p0, p1, p3);

	// 3) Skip any flip where either area is near zero
	const double FLAT_EPS = 1e-10;
	if (a0 < FLAT_EPS || a1 < FLAT_EPS) {
		return false;
	} 
	// -----------------

	// DEBUG 1: Print the four corner vertex indices
#ifndef NDEBUG
	std::cout << "[flipEdge] Quad verts: v0=" << v0
		<< ", v1=" << v1
		<< ", v2=" << v2
		<< ", v3=" << v3 << "\n";
#endif
	
	// 6) Incircle/Delaunay test
	{
		glm::dvec2 v01 = p1 - p0, v02 = p2 - p0, v03 = p3 - p0;
		double det = (v01.x * v01.x + v01.y * v01.y) * cross2d(v02, v03)
			- (v02.x * v02.x + v02.y * v02.y) * cross2d(v01, v03)
			+ (v03.x * v03.x + v03.y * v03.y) * cross2d(v01, v02);
		if (det >= 0.0) {
#ifndef NDEBUG
			std::cout << "[flipEdge] Already Delaunay, skipping\n";
#endif
			return false;
		}
	}

	// 7) Degeneracy and convexity check
	double area0 = std::abs(cross2d(p1 - p0, p2 - p0)) * 0.5;
	double area1 = std::abs(cross2d(p1 - p0, p3 - p0)) * 0.5;
	const double EPS = 1e-10;
	if (area0 < EPS || area1 < EPS) {
#ifndef NDEBUG
		std::cout << "[flipEdge] Degenerate layout, skipping\n";
#endif
		return false;
	}
	double s2 = cross2d(p1 - p0, p2 - p0);
	double s3 = cross2d(p1 - p0, p3 - p0);
	if (s2 * s3 >= 0.0) {
#ifndef NDEBUG
		std::cout << "[flipEdge] Quad not convex, skipping\n";
#endif
		return false;
	}

	// 8) Compute new diagonal length
	double newLen = glm::length(p3 - p2);
	if (newLen <= EPS) {
#ifndef NDEBUG
		std::cout << "[flipEdge] New edge length too small, skipping\n";
#endif
		return false;
	}

	
	// 9) Log attempt
#ifndef NDEBUG
	std::cout << "[flipEdge] Flipping diagonal he" << diagonalHE << " (faces " << f0 << "," << f1 << ")\n";
#endif

	// 10) Backup
	auto oldHalfEdges = halfEdges;
	auto oldVertices = vertices;
	auto oldEdges = edges;

	uint32_t oldF0HalfEdge = faces[f0].halfEdgeIdx;
	uint32_t oldF1HalfEdge = faces[f1].halfEdgeIdx;

	// 11) Save old opposites 
	uint32_t oldOpps[6] = {
		halfEdges[diagonalHE].opposite,
		halfEdges[internal1HE].opposite,
		halfEdges[internal2HE].opposite,
		halfEdges[diagonal2HE].opposite,
		halfEdges[internal3HE].opposite,
		halfEdges[internal4HE].opposite
	};
	uint32_t heIdxs[6] = { diagonalHE, internal1HE, internal2HE, diagonal2HE, internal3HE, internal4HE };
	
	// DEBUG 2: Print halfedge, opposites and corresponding start/endpoints
#ifndef NDEBUG
	std::cout << "[flipEdge] Original halfedge info:\n";
#endif
	for (int i = 0; i < 6; ++i) {
		uint32_t h = heIdxs[i], o = oldOpps[i];
		// Original start/endpoints
		uint32_t a1 = halfEdges[h].origin;
		uint32_t b1 = halfEdges[halfEdges[h].next].origin;
		// Original opposite start/endpoints
		uint32_t a2 = INVALID_INDEX, b2 = INVALID_INDEX;
		if (o != INVALID_INDEX) {
			a2 = halfEdges[o].origin;
			b2 = halfEdges[halfEdges[o].next].origin;
		}
#ifndef NDEBUG
		std::cout << "   he" << h << "=(" << a1 << "-" << b1 << ") oldOpp=" << o << "=(" << a2 << "-" << b2 << ")\n";
#endif

	}
	
	// 12) COMBINATORIAL FLIPS
	// 
	// diagonalHE becomes (v3 -> v2) in face f0
	diagonal1.origin = v3;  // 3
	diagonal1.next = internal1HE; 
	diagonal1.face = f0;

	// diagonal2HE becomes (v2 -> v3) in face f1
	diagonal2.origin = v2;   
	diagonal2.next = internal3HE;  
	diagonal2.face = f1;

	// Update the four internal halfedges
	// internal1HE 
	halfEdges[internal1HE].origin = v2;  
	halfEdges[internal1HE].next = internal2HE;  
	halfEdges[internal1HE].face = f0;

	// internal2HE 
	halfEdges[internal2HE].origin = v0;  
	halfEdges[internal2HE].next = diagonalHE;  
	halfEdges[internal2HE].face = f0;

	// internal3HE 
	halfEdges[internal3HE].origin = v3; 
	halfEdges[internal3HE].next = internal4HE;  
	halfEdges[internal3HE].face = f1;  

	// internal4HE 
	halfEdges[internal4HE].origin = v1;  
	halfEdges[internal4HE].next = diagonal2HE;  
	halfEdges[internal4HE].face = f1;

	// Update prev pointers
	halfEdges[diagonalHE].prev = internal2HE;
	halfEdges[internal1HE].prev = diagonalHE;
	halfEdges[internal2HE].prev = internal1HE;

	halfEdges[diagonal2HE].prev = internal4HE;
	halfEdges[internal3HE].prev = diagonal2HE;
	halfEdges[internal4HE].prev = internal3HE;

	// 12b) Rebuild opposites after rewire
	rebuildOpposites();

	// 12c) Recalculate lengths
	updateIntrinsicLength(diagonalHE);
	updateIntrinsicLength(diagonal2HE);
	updateIntrinsicLength(internal1HE);
	updateIntrinsicLength(internal2HE);
	updateIntrinsicLength(internal3HE);
	updateIntrinsicLength(internal4HE);

	// 13) Update face pointers
	faces[f0].halfEdgeIdx = diagonalHE;
	faces[f1].halfEdgeIdx = diagonal2HE;

#ifndef NDEBUG

	std::cout << "[flipEdge] After rewire:\n";

	std::cout << "  NEW diagonal: he" << diagonalHE

		<< "(" << halfEdges[diagonalHE].origin
		<< "-" << halfEdges[halfEdges[diagonalHE].next].origin
		<< ")\n";

	std::cout << "  NEW diagonal opposite: he" << diagonal2HE
		<< "(" << halfEdges[diagonal2HE].origin
		<< "-" << halfEdges[halfEdges[diagonal2HE].next].origin
		<< ")\n";

	std::cout << "  Internal halfedges (6 total - 2 triangles):\n";
	std::cout << "  Triangle f0:\n";
	std::cout << "    he" << diagonalHE << " diagonal=("
		<< halfEdges[diagonalHE].origin << "-"
		<< halfEdges[halfEdges[diagonalHE].next].origin
		<< ")\n";

	std::cout << "    he" << internal1HE << " internal=("
		<< halfEdges[internal1HE].origin << "-"
		<< halfEdges[halfEdges[internal1HE].next].origin
		<< ")\n";

	std::cout << "    he" << internal2HE << " internal=("     
		<< halfEdges[internal2HE].origin << "-"
		<< halfEdges[halfEdges[internal2HE].next].origin
		<< ")\n";

	std::cout << "  Triangle f1:\n";

	std::cout << "    he" << diagonal2HE << " diagonal=("
		<< halfEdges[diagonal2HE].origin << "-"
		<< halfEdges[halfEdges[diagonal2HE].next].origin
		<< ")\n";
	std::cout << "    he" << internal3HE << " internal=("     
		<< halfEdges[internal3HE].origin << "-"
		<< halfEdges[halfEdges[internal3HE].next].origin
		<< ")\n";

	std::cout << "    he" << internal4HE << " internal=("
		<< halfEdges[internal4HE].origin << "-"
		<< halfEdges[halfEdges[internal4HE].next].origin
		<< ")\n";

	std::cout << "  Rim edges:\n";
#endif
	
	// The rim halfedges are the opposites of the 4 internal (non-diagonal) halfedges
	std::vector<uint32_t> rimHalfEdges;
#ifndef NDEBUG
	std::cout << "[flipEdge DEBUG] dumping rim twins for diagonalHE=" << diagonalHE << "\n";
#endif
	for (auto h : { internal1HE, internal2HE, internal3HE, internal4HE }) {
		uint32_t rimHE = halfEdges[h].opposite;
		if (rimHE != INVALID_INDEX) {
			rimHalfEdges.push_back(rimHE);
			uint32_t a1 = halfEdges[rimHE].origin;
			uint32_t b1 = halfEdges[halfEdges[rimHE].next].origin;
#ifndef NDEBUG

			std::cout << "   inHE=" << h
				<< " rimHE=" << rimHE
				<< " edge=(" << a1 << "-" << b1 << ")\n";
#endif
		}
		else {
#ifndef NDEBUG
			std::cout << "    boundary edge (no rim halfedge)\n";
#endif
		}
	}
	
	// Local function to dump a face's halfedges, origins and opposites
	auto dumpTriangle = [&](uint32_t faceIdx, const char* tag) {
		auto hes = getFaceHalfEdges(faceIdx);
#ifndef NDEBUG
		std::cout << tag << "face " << faceIdx << ":";
#endif
		for (uint32_t h : hes) {
			const auto& e = halfEdges[h];
#ifndef NDEBUG
			std::cout << " [he" << h << " origin=" << e.origin << " op=" << e.opposite << "]";
#endif
		}
#ifndef NDEBUG
		std::cout << "\n";
#endif
		};

	// 16) Validate the new faces
	auto validateFace = [&](uint32_t faceIdx) {
		auto faceVerts = getFaceVertices(faceIdx);
		if (faceVerts.size() != 3) {
#ifndef NDEBUG
			std::cerr << "[flipEdge] Flip validation failed: Face " << faceIdx
				<< " has " << faceVerts.size() << " vertices (expected 3)\n";
#endif
			return false;
		}

		std::sort(faceVerts.begin(), faceVerts.end());
		if (std::adjacent_find(faceVerts.begin(), faceVerts.end()) != faceVerts.end()) {
#ifndef NDEBUG
			std::cerr << "[flipEdge] Flip validation failed: Face " << faceIdx
				<< " has duplicate vertices\n";
#endif
			return false;
		}

		return true;
		};

	if (!validateFace(f0) || !validateFace(f1)) {
#ifndef NDEBUG
		std::cerr << "[flipEdge] Face validation failed, restoring previous state.\n";
#endif
		halfEdges = std::move(oldHalfEdges);
		vertices = std::move(oldVertices);
		edges = std::move(oldEdges);
		faces[f0].halfEdgeIdx = oldF0HalfEdge;
		faces[f1].halfEdgeIdx = oldF1HalfEdge;
		return false;
	}

	// DEBUG should remove soon
	auto dumpHE = [&](uint32_t h) {
		const auto& e = halfEdges[h];
		uint32_t nxt = e.next, prv = e.prev, opp = e.opposite;
#ifndef NDEBUG
		std::cerr << "[flipEdge-debug] he" << h
			<< " origin=" << e.origin
			<< " next=" << nxt
			<< " prev=" << prv
			<< " opp=" << opp << "\n";
#endif
		};

#ifndef NDEBUG
	std::cerr << "[flipEdge-debug] POST-REWIRE pointers:\n";
#endif
	for (auto hh : { diagonalHE, diagonal2HE,
					 internal1HE, internal2HE,
					 internal3HE, internal4HE }) {
		dumpHE(hh);
		// also dump its rim twin, if any:
		uint32_t rim = halfEdges[hh].opposite;
		if (rim != INVALID_INDEX) dumpHE(rim);
	}

	// 17) Rebuild edges before manifold check
	rebuildEdges();
	// 18) Rebuild connectivity 
	rebuildConnectivity();

	// 19b) Check vertex anchor consistency
#ifndef NDEBUG
	std::cout << "\n=== VERTEX ANCHOR CHECK AFTER REWIRE ===" << std::endl;
#endif
	for (uint32_t v : {v0, v1, v2, v3}) {
		uint32_t anchor = vertices[v].halfEdgeIdx;
#ifndef NDEBUG
		std::cout << "Vertex " << v << " anchor: he" << anchor;
#endif

		if (anchor == INVALID_INDEX) {
#ifndef NDEBUG
			std::cout << " (INVALID)" << std::endl;
#endif
		}
		else if (anchor >= halfEdges.size()) {
#ifndef NDEBUG
			std::cout << " (OUT OF BOUNDS)" << std::endl;
#endif
		}
		else {
			uint32_t anchorOrigin = halfEdges[anchor].origin;
#ifndef NDEBUG
			std::cout << " (origin=" << anchorOrigin << ")";
#endif
			if (anchorOrigin != v) {
#ifndef NDEBUG
				std::cout << " ERROR: Anchor origin mismatch!";
#endif
			}
			std::cout << std::endl;
		}
	}

#ifndef NDEBUG
	  auto dumpFan = [&](uint32_t v) {
		  std::cout << "  fan at v" << v << ":";
		  for (auto he : getVertexHalfEdges(v)) {
			  // sanity check:
			  if (halfEdges[he].origin != v) {
				  std::cerr << "[dumpFanERR] he" << he << " has origin "
					  << halfEdges[he].origin << " but we asked for fan of " << v << "\n";
				  continue;
			  }
			  uint32_t to = halfEdges[halfEdges[he].next].origin;
			  std::cout << " he" << he << "(" << v << "->" << to << ")";
		  }
		  std::cout << "\n";
		  };

    dumpFan(v0);
    dumpFan(v2);
#endif

	// 20) Check manifoldness, bail out if not manifold
	if (!isManifold()) {
		std::cerr << "[flipEdge] Flip would break manifold, aborting.\n";
		halfEdges = std::move(oldHalfEdges);
		vertices = std::move(oldVertices);
		edges = std::move(oldEdges);
		faces[f0].halfEdgeIdx = oldF0HalfEdge;
		faces[f1].halfEdgeIdx = oldF1HalfEdge;
		return false;
	}

	// DEBUG should remove soon
#ifndef NDEBUG
	auto dumpIncidentCount = [&](uint32_t h) {
		uint32_t a = halfEdges[h].origin;
		uint32_t b = halfEdges[halfEdges[h].next].origin;

		// Find all halfedges going a->b
		int count = 0;
		std::unordered_set<uint32_t> visited;

		for (uint32_t i = 0; i < halfEdges.size(); ++i) {
			if (halfEdges[i].origin == a) {
				uint32_t nextHE = halfEdges[i].next;
				if (nextHE < halfEdges.size() && halfEdges[nextHE].origin == b) {
					count++;
					visited.insert(i);
				}
			}
		}

		std::cerr << "[flipEdge-debug] halfedge he" << h
			<< " (" << a << "->" << b << ")"
			<< " incidentFaces=" << count << "\n";

		// Also print which halfedges these are
		std::cerr << "  Halfedges going " << a << "->" << b << ": ";
		for (uint32_t he : visited) {
			std::cerr << "he" << he << " ";
		}
		std::cerr << "\n";
		};
#endif

#ifndef NDEBUG
	// dump the six you just rewired:
	std::cerr << "[flipEdge-debug] INCIDENT COUNTS BEFORE MANIFOLD CHECK:\n";
	for (auto hh : { diagonalHE, diagonal2HE,
					 internal1HE, internal2HE,
					 internal3HE, internal4HE })
		dumpIncidentCount(hh);
#endif
	
#ifndef NDEBUG
	// FINAL DEBUG DUMPS AFTER FLIP
	std::cout << "[flipEdge] AFTER flip HE" << diagonalHE << " \n";
	dumpTriangle(f0, "    ");
	dumpTriangle(f1, "    ");
#endif

#ifndef NDEBUG
	std::cout << "[flipEdge] Flip succeeded on he" << diagonalHE
		<< " (verts: " << v0 << "," << v1 << "," << v2 << "," << v3 << ")\n";
#endif
	
	return true;
}

bool HalfEdgeMesh::isDelaunayEdge(uint32_t heIdx) const {
	// 1) Boundary edges are always delaunay
	if (heIdx >= halfEdges.size()) return true;
	const HalfEdge& he = halfEdges[heIdx];
	if (he.opposite == INVALID_INDEX) return true;

	// 2) Layout the quad around this halfedge
	auto quad = layoutDiamond(heIdx);
	const glm::dvec2& p0 = quad[0], & p1 = quad[1], & p2 = quad[2], & p3 = quad[3];

	// 3) Precompute squared norms
	double p0_sq = double(p0.x) * p0.x + double(p0.y) * p0.y;
	double p1_sq = double(p1.x) * p1.x + double(p1.y) * p1.y;
	double p2_sq = double(p2.x) * p2.x + double(p2.y) * p2.y;
	double p3_sq = double(p3.x) * p3.x + double(p3.y) * p3.y;

	// 4) Compute the 4x4 incircle determinant via cofactor expansion
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

	// 5) If det > 0, p3 is inside therefore not Delaunay
	const double EPS = 1e-10;
	return det <= EPS;
}

int HalfEdgeMesh::makeDelaunay(int maxIterations) {
	rebuildConnectivity();
	int totalFlips = 0;

	std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> flippedAB;

	for (int iter = 0; iter < maxIterations; ++iter) {
		std::queue<uint32_t> queueHE;
		std::unordered_set<uint32_t> inQueue;

		// 1) Enqueue all non-Delaunay halfedges
		auto Es = getEdges();
		for (uint32_t ei = 0; ei < Es.size(); ++ei) {
			uint32_t he = Es[ei].halfEdgeIdx;
			if (he != INVALID_INDEX && !isDelaunayEdge(he)) {
				queueHE.push(he);
				inQueue.insert(he);
			}
		}

		if (queueHE.empty()) break;

		// 2) Process 
		int flipsThisIter = 0;
		while (!queueHE.empty()) {
			uint32_t he = queueHE.front();
			queueHE.pop();
			inQueue.erase(he);

			if (he >= halfEdges.size() || isDelaunayEdge(he))
				continue;

			// Avoid flipping the same undirected edge twice
			auto vA = halfEdges[he].origin;
			auto vB = halfEdges[halfEdges[he].next].origin;
			auto key = std::minmax(vA, vB);
			if (flippedAB.count(key)) continue;

			if (flipEdge(he)) {
				++flipsThisIter;
				++totalFlips;
				flippedAB.insert(key);
				// Enqueue neighbors
				for (auto nhe : getNeighboringHalfEdges(he)) {
					if (!inQueue.count(nhe)) {
						queueHE.push(nhe);
						inQueue.insert(nhe);
					}
				}
			}
		}

		// 3) Finished if no more flips
		if (flipsThisIter == 0)
			break;

		// 4) Prepare for next iteration
		rebuildEdges();
		rebuildConnectivity();
		flippedAB.clear();
	}

	return totalFlips;
}

uint32_t HalfEdgeMesh::addIntrinsicVertex() {
	Vertex newVertex;
	newVertex.position = glm::vec3(0.0f); 
	newVertex.halfEdgeIdx = INVALID_INDEX;
	newVertex.originalIndex = vertices.size();

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
	halfEdges[newHe10].intrinsicLength = r1;
	halfEdges[newHe01].origin = newV;
	halfEdges[newHe01].next = he0;
	halfEdges[newHe01].prev = newHe10;
	halfEdges[newHe01].opposite = newHe02;
	halfEdges[newHe01].face = f1;
	halfEdges[newHe01].intrinsicLength = r0;

	// Triangle 2: (v1->v2->newV) 
	halfEdges[he1].next = newHe21;
	halfEdges[he1].prev = newHe12;
	halfEdges[he1].face = f2;
	halfEdges[newHe21].origin = v2;
	halfEdges[newHe21].next = newHe12;
	halfEdges[newHe21].prev = he1;
	halfEdges[newHe21].opposite = newHe20;
	halfEdges[newHe21].face = f2;
	halfEdges[newHe21].intrinsicLength = r2;
	halfEdges[newHe12].origin = newV;
	halfEdges[newHe12].next = he1;
	halfEdges[newHe12].prev = newHe21;
	halfEdges[newHe12].opposite = newHe10;
	halfEdges[newHe12].face = f2;
	halfEdges[newHe12].intrinsicLength = r1;

	// Triangle 3: (v2->v0->newV) 
	halfEdges[he2].next = newHe02;
	halfEdges[he2].prev = newHe20;
	halfEdges[he2].face = f3;
	halfEdges[newHe02].origin = v0;
	halfEdges[newHe02].next = newHe20;
	halfEdges[newHe02].prev = he2;
	halfEdges[newHe02].opposite = newHe01;
	halfEdges[newHe02].face = f3;
	halfEdges[newHe02].intrinsicLength = r0;
	halfEdges[newHe20].origin = newV;
	halfEdges[newHe20].next = he2;
	halfEdges[newHe20].prev = newHe02;
	halfEdges[newHe20].opposite = newHe21;
	halfEdges[newHe20].face = f3;
	halfEdges[newHe20].intrinsicLength = r2;

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
	edges[newEdge0].intrinsicLength = halfEdges[newHe01].intrinsicLength;
	edges[newEdge0].isOriginal = false;

	uint32_t newEdge1 = edges.size();
	edges.emplace_back(newHe12);
	edges[newEdge1].intrinsicLength = halfEdges[newHe12].intrinsicLength;
	edges[newEdge1].isOriginal = false;

	uint32_t newEdge2 = edges.size();
	edges.emplace_back(newHe20);
	edges[newEdge2].intrinsicLength = halfEdges[newHe20].intrinsicLength;
	edges[newEdge2].isOriginal = false;

	// Update faces and vertex
	faces[f1].halfEdgeIdx = he0;
	faces[f2].halfEdgeIdx = he1;
	faces[f3].halfEdgeIdx = he2;
	vertices[newV].halfEdgeIdx = newHe01;
#ifndef NDEBUG
	// Debug zero length check
	for (auto he : { newHe01, newHe10, newHe12, newHe21, newHe02, newHe20 }) {
		if (halfEdges[he].intrinsicLength <= 0.0)
			std::cerr << "[BUG] zero length he" << he << std::endl;
	}
#endif
	return newV;
}

uint32_t HalfEdgeMesh::insertVertexAlongEdge(uint32_t edgeIdx) {
#ifndef NDEBUG
	std::cout << "[insertVertexAlongEdge] Starting with edgeIdx=" << edgeIdx << std::endl;
#endif

	if (edgeIdx >= edges.size()) {
#ifndef NDEBUG
		std::cout << "[insertVertexAlongEdge] ERROR: edgeIdx " << edgeIdx << " >= edges.size() " << edges.size() << std::endl;
#endif
		return INVALID_INDEX;
	}

	// Fetch the two halfedges of this edge
	uint32_t heA = edges[edgeIdx].halfEdgeIdx;
	uint32_t heB = halfEdges[heA].opposite;

	//std::cout << "[insertVertexAlongEdge] Original edge halfedges: heA=" << heA << ", heB=" << heB << std::endl;

	if (heB == INVALID_INDEX) {
		// Boundary edge
#ifndef NDEBUG
		std::cout << "[insertVertexAlongEdge] ERROR: Boundary edge (heB=INVALID)" << std::endl;
#endif
		return INVALID_INDEX;
	}

	// Faces on each side
	uint32_t fA = halfEdges[heA].face;
	uint32_t fB = halfEdges[heB].face;
#ifndef NDEBUG
	std::cout << "[insertVertexAlongEdge] Faces: fA=" << fA << ", fB=" << fB << std::endl;
#endif
	// Original vertices
	uint32_t vOrigA = halfEdges[heA].origin;
	uint32_t vOrigB = halfEdges[heB].origin;

	// Create new vertex
	uint32_t newV = vertices.size();
	vertices.emplace_back();
	vertices[newV].halfEdgeIdx = heA;

#ifndef NDEBUG
	std::cout << "[insertVertexAlongEdge] Created new vertex: newV=" << newV << std::endl;
#endif
	// Create new halfedge pair heAnew <-> heBnew
	uint32_t heAnew = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t heBnew = halfEdges.size();
	halfEdges.emplace_back();

#ifndef NDEBUG
	std::cout << "[insertVertexAlongEdge] Created new halfedges: heAnew=" << heAnew << ", heBnew=" << heBnew << std::endl;
#endif

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

	// Update vertex halfedge pointer
	vertices[newV].halfEdgeIdx = heA;
	vertices[vOrigA].halfEdgeIdx = heAnew;
#ifndef NDEBUG
	std::cout << "[insertVertexAlongEdge] SUCCESS: Returning heAnew=" << heAnew << std::endl;
#endif
	return heAnew;
}

uint32_t HalfEdgeMesh::connectVertices(uint32_t heA, uint32_t heB) {
#ifndef NDEBUG
	std::cout << "[connectVertices] Starting with heA=" << heA << ", heB=" << heB << std::endl;
#endif

	// Validate inputs
	if (heA >= halfEdges.size() || heB >= halfEdges.size()) {
#ifndef NDEBUG
		std::cout << "[connectVertices] ERROR: Invalid halfedge indices" << std::endl;
#endif
		return INVALID_INDEX;
	}

	uint32_t vA = halfEdges[heA].origin;
	uint32_t vB = halfEdges[heB].origin;
	uint32_t faceOrig = halfEdges[heA].face;

#ifndef NDEBUG
	std::cout << "[connectVertices] Connecting vertices: vA=" << vA << ", vB=" << vB << " in face=" << faceOrig << std::endl;
#endif

	// DEBUG: Print the original face before modification
#ifndef NDEBUG
	std::cout << "[DEBUG] BEFORE - Face " << faceOrig << " vertices: ";
#endif
	uint32_t walkHE = faces[faceOrig].halfEdgeIdx;
	for (int i = 0; i < 10 && walkHE != INVALID_INDEX; i++) {
#ifndef NDEBUG
		std::cout << halfEdges[walkHE].origin << " ";
#endif
		walkHE = halfEdges[walkHE].next;
		if (walkHE == faces[faceOrig].halfEdgeIdx) break;
	}
	std::cout << std::endl;


	// Create new halfedge pair diagA <-> diagB
	uint32_t diagA = halfEdges.size();
	halfEdges.emplace_back();
	uint32_t diagB = halfEdges.size();
	halfEdges.emplace_back();

#ifndef NDEBUG
	std::cout << "[connectVertices] Created diagonal halfedges: diagA=" << diagA << ", diagB=" << diagB << std::endl;
#endif

	halfEdges[diagA].opposite = diagB;
	halfEdges[diagB].opposite = diagA;

	// They split the face of heA (and heB)
	uint32_t fOld = halfEdges[heA].face;
	uint32_t fNew = faces.size();
	faces.emplace_back();

#ifndef NDEBUG
	std::cout << "[connectVertices] Splitting face: fOld=" << fOld << " -> fNew=" << fNew << std::endl;
#endif

	// Set origins
	halfEdges[diagA].origin = halfEdges[heA].origin;
	halfEdges[diagB].origin = halfEdges[heB].origin;

#ifndef NDEBUG
	std::cout << "[connectVertices] Set diagonal origins: diagA.origin=" << halfEdges[diagA].origin
		<< ", diagB.origin=" << halfEdges[diagB].origin << std::endl;
#endif

	// Store the previous pointers before we modify them
	uint32_t heAprev = halfEdges[heA].prev;
	uint32_t heBprev = halfEdges[heB].prev;

#ifndef NDEBUG
	std::cout << "[connectVertices] Previous pointers: heAprev=" << heAprev << ", heBprev=" << heBprev << std::endl;
#endif

	// DEBUG: Show what we're about to remap
#ifndef NDEBUG
	std::cout << "[connectVertices] About to remap faces from heB=" << heB << " to heA=" << heA << std::endl;
#endif
#ifndef NDEBUG
	std::cout << "[connectVertices] Current face assignments before remap:" << std::endl;
#endif
	uint32_t cursor = heB;
	int walkCount = 0;
	do {
#ifndef NDEBUG
		std::cout << "[connectVertices]   he" << cursor << ": origin=" << halfEdges[cursor].origin
			<< ", face=" << halfEdges[cursor].face << ", next=" << halfEdges[cursor].next << std::endl;
#endif
		cursor = halfEdges[cursor].next;
		walkCount++;
		if (walkCount > 10) break;
	} while (cursor != heA && cursor != INVALID_INDEX);

	// Remap faces: walk from heB to heA (exclusive), marking fNew
#ifndef NDEBUG
	std::cout << "[connectVertices] Remapping faces from heB to heA..." << std::endl;
#endif
	cursor = heB;
	walkCount = 0;
	do {
#ifndef NDEBUG
		std::cout << "[connectVertices]   Setting he" << cursor << ".face = " << fNew << std::endl;
#endif
		halfEdges[cursor].face = fNew;
		cursor = halfEdges[cursor].next;
		walkCount++;
		if (walkCount > 10) {
#ifndef NDEBUG
			std::cout << "[connectVertices] WARNING: Face walk exceeded 10 steps, breaking" << std::endl;
#endif
			break;
		}
	} while (cursor != heA);

	// Connect diagA: heAprev -> diagA -> heB
#ifndef NDEBUG
	std::cout << "[connectVertices] Connecting diagA: " << heAprev << " -> " << diagA << " -> " << heB << std::endl;
#endif
	halfEdges[heAprev].next = diagA;
	halfEdges[diagA].prev = heAprev;
	halfEdges[diagA].next = heB;
	halfEdges[heB].prev = diagA;
	halfEdges[diagA].face = fOld;

	// Connect diagB: heBprev -> diagB -> heA
#ifndef NDEBUG
	std::cout << "[connectVertices] Connecting diagB: " << heBprev << " -> " << diagB << " -> " << heA << std::endl;
#endif
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
#ifndef NDEBUG
	std::cout << "[connectVertices] Updated face anchors: faces[" << fOld << "].halfEdgeIdx=" << diagA
		<< ", faces[" << fNew << "].halfEdgeIdx=" << diagB << std::endl;

	std::cout << "[connectVertices] SUCCESS: Returning diagA=" << diagA << std::endl;
#endif
	return diagA;
}

HalfEdgeMesh::Split HalfEdgeMesh::splitEdgeTopo(uint32_t edgeIdx, double t) {
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] edgeIdx=" << edgeIdx << ", t=" << t << std::endl;
#endif
	if (edgeIdx >= edges.size()) {
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] ERROR: edgeIdx " << edgeIdx << " >= edges.size() " << edges.size() << std::endl;
#endif
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	// Store original edge info
	uint32_t originalHE = edges[edgeIdx].halfEdgeIdx;
	double originalLength = halfEdges[originalHE].intrinsicLength;

	//std::cout << "[splitEdgeTopo] Original edge: halfedge=" << originalHE << ", length=" << originalLength << std::endl;

	// Get original vertices for debugging
	uint32_t vOrig1 = halfEdges[originalHE].origin;
	uint32_t vOrig2 = halfEdges[halfEdges[originalHE].next].origin;
	//std::cout << "[splitEdgeTopo] Original edge connects vertices: " << vOrig1 << " -> " << vOrig2 << std::endl;

	// 1) Split the original edge into a quad on each side
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Step 1: Inserting vertex along edge..." << std::endl;
#endif
	uint32_t heFront = insertVertexAlongEdge(edgeIdx);

	if (heFront == INVALID_INDEX) {
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] ERROR: insertVertexAlongEdge failed" << std::endl;
#endif
		return { INVALID_INDEX, INVALID_INDEX, INVALID_INDEX };
	}

	uint32_t heBack = halfEdges[heFront].opposite;

	// Find the new vertex 
	uint32_t newV = halfEdges[halfEdges[heFront].next].origin;
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] After vertex insertion: heFront=" << heFront << ", heBack=" << heBack << ", newV=" << newV << std::endl;
#endif
	// 2) Draw the diagonal in each quad to form triangles
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Step 2: Drawing diagonals to form triangles..." << std::endl;
#endif
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

	// 3) Set intrinsic lengths for the two child halfedges
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Step 3: Setting intrinsic lengths..." << std::endl;
#endif
	// Calculate lengths based on split fraction
	double lengthA = t * originalLength;        // First part: vOrig1 -> newV
	double lengthB = (1.0 - t) * originalLength; // Second part: newV -> vOrig2

#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Split fraction t=" << t << ", originalLength=" << originalLength << std::endl;
	std::cout << "[splitEdgeTopo] lengthA (first part)=" << lengthA << ", lengthB (second part)=" << lengthB << std::endl;
#endif
	// Set the two halfedges of the edge split
	uint32_t child1 = heFront;  // vOrig1 -> newV (left child)
	uint32_t child2 = originalHE; // newV -> vOrig2  (right child)
#ifndef NDEBUG
	// DEBUG
	std::cout << "[splitEdgeTopo] child1=" << child1
		<< " origin=" << halfEdges[child1].origin
		<< " next.origin=" << halfEdges[halfEdges[child1].next].origin
		<< std::endl;
	std::cout << "[splitEdgeTopo] child2=" << child2
		<< " origin=" << halfEdges[child2].origin
		<< " next.origin=" << halfEdges[halfEdges[child2].next].origin
		<< std::endl;
#endif
	// Assign lengths correctly
	halfEdges[child1].intrinsicLength = lengthA;  // vOrig1 -> newV
	halfEdges[child2].intrinsicLength = lengthB;  // newV -> vOrig2
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Assigned lengthA=" << lengthA << " to child1=" << child1 << std::endl;
	std::cout << "[splitEdgeTopo] Assigned lengthB=" << lengthB << " to child2=" << child2 << std::endl;
#endif
	// Update opposites to match
	if (halfEdges[child1].opposite != INVALID_INDEX) {
		halfEdges[halfEdges[child1].opposite].intrinsicLength = lengthA;
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] Updated opposite of child1: he" << halfEdges[child1].opposite << " = " << lengthA << std::endl;
#endif
	}
	if (halfEdges[child2].opposite != INVALID_INDEX) {
		halfEdges[halfEdges[child2].opposite].intrinsicLength = lengthB;
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] Updated opposite of child2: he" << halfEdges[child2].opposite << " = " << lengthB << std::endl;
#endif
	}

	// Update Edge members
	edges[edgeIdx].halfEdgeIdx = child1;
	edges[edgeIdx].intrinsicLength = lengthA;

	uint32_t newEdgeIdx = edges.size();
	edges.emplace_back(child2);
	edges[newEdgeIdx].intrinsicLength = lengthB;

	// Child splits are no longer original edges
	edges[edgeIdx].isOriginal = false;
	edges[newEdgeIdx].isOriginal = false;
#ifndef NDEBUG
	std::cout << "[splitEdgeTopo] Updated original edge " << edgeIdx << ": halfedge=" << child1 << ", length=" << lengthA << std::endl;
	std::cout << "[splitEdgeTopo] Created new edge " << newEdgeIdx << ": halfedge=" << child2 << ", length=" << lengthB << std::endl;
#endif

	// Create Edge entries for the diagonals
	if (diagFront != INVALID_INDEX) {
		uint32_t diagFrontEdgeIdx = edges.size();
		edges.emplace_back(diagFront);
		edges[diagFrontEdgeIdx].intrinsicLength = 0.0; 
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] Created edge " << diagFrontEdgeIdx << " for diagFront=" << diagFront << std::endl;
#endif
	}

	if (diagBack != INVALID_INDEX) {
		uint32_t diagBackEdgeIdx = edges.size();
		edges.emplace_back(diagBack);
		edges[diagBackEdgeIdx].intrinsicLength = 0.0; 
#ifndef NDEBUG
		std::cout << "[splitEdgeTopo] Created edge " << diagBackEdgeIdx << " for diagBack=" << diagBack << std::endl;
#endif
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

	rebuildOpposites();

	return { newV, child1, child2, diagFront, diagBack };
}

void HalfEdgeMesh::removeVertex(uint32_t v) {
	if (v >= vertices.size()) {
#ifndef NDEBUG
#ifndef NDEBUG
		std::cerr << "[removeVertex] Error: vertex index " << v
			<< " out of range (0.." << vertices.size() - 1 << ")\n";
#endif
#endif
		return;
	}

	uint32_t last = static_cast<uint32_t>(vertices.size()) - 1;
	if (v != last) {
#ifndef NDEBUG
#ifndef NDEBUG
		std::cerr << "[removeVertex] Error: can only remove the last vertex ("
			<< last << "), not " << v << "\n";
#endif
#endif
		return;
	}

	auto hes = getVertexHalfEdges(v);
	if (!hes.empty()) {
#ifndef NDEBUG
#ifndef NDEBUG
		std::cerr << "[removeVertex] Error: vertex " << v
			<< " still has " << hes.size() << " incident halfedges\n";
#endif
#endif
		return;
	}

	vertices.pop_back();
}

std::vector<uint32_t> HalfEdgeMesh::getVertexHalfEdges(uint32_t v) const {
	std::vector<uint32_t> fan;
	if (v >= vertices.size()) 
		return fan;

	uint32_t startHE = vertices[v].halfEdgeIdx;
	if (startHE == INVALID_INDEX) 
		return fan;

	if (startHE >= halfEdges.size()) {
#ifndef NDEBUG
#ifndef NDEBUG
		std::cerr << "[getVertexHalfEdges] ERROR: vertex " << v
			<< " has invalid halfEdgeIdx " << startHE << std::endl;
#endif
#endif
		return fan;
	}

	if (halfEdges[startHE].origin != v) {
#ifndef NDEBUG
		std::cerr << "[getVertexHalfEdges] WARNING: he" << startHE
			<< " has origin " << halfEdges[startHE].origin
			<< " but expected " << v << std::endl;
#endif

		// Search for correct anchor
#ifndef NDEBUG
		std::cerr << "  Searching for correct halfedge for vertex " << v << "..." << std::endl;
#endif
		for (uint32_t i = 0; i < halfEdges.size(); ++i) {
			if (halfEdges[i].origin == v) {
#ifndef NDEBUG
				std::cerr << "  Found correct halfedge: he" << i << std::endl;
#endif
				break;
			}
		}
	}

	uint32_t he = startHE;
	do {
		// Walk debugging
		//std::cerr << "[getVertexHalfEdges] visiting he" << he
		//	<< " origin=" << halfEdges[he].origin << "\n";

		// only collect if it really starts at v
		if (halfEdges[he].origin != v) {
#ifndef NDEBUG
			std::cerr << "[getVertexHalfEdges] WARNING: he"
				<< he << " has origin "
				<< halfEdges[he].origin
				<< " but expected " << v << "\n";
#endif
			break;
		}
		fan.push_back(he);

		// step across the opposite, then around the next face
		uint32_t opp = halfEdges[he].opposite;
		// Walk debugging
		//std::cerr << "[getVertexHalfEdges]  opp = he" << opp << "\n";
		if (opp == INVALID_INDEX) {
			std::cerr << "[getVertexHalfEdges]  hit boundary, bailing.\n";
			break; // hit boundary
		}
		
		uint32_t next = halfEdges[opp].next;
		// Walk debugging
		//std::cerr << "[getVertexHalfEdges]  next = he" << next << "\n";
		if (next == INVALID_INDEX) {
			std::cerr << "[getVertexHalfEdges]  next invalid, bailing.\n";
			break; // safety check
		}
		// loop until we come back
		he = next;

	} while (he != startHE);

	return fan;
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
#ifndef NDEBUG
#ifndef NDEBUG
			std::cerr << "Invalid halfedge index " << currentIdx << " in getFaceVertices\n";
#endif
#endif
			break;
		}

		vertexIndices.push_back(halfEdges[currentIdx].origin);

		currentIdx = halfEdges[currentIdx].next;

		// Safety check to prevent infinite loops
		if (++safetyCounter > MAX_ITERATIONS) {
#ifndef NDEBUG
#ifndef NDEBUG
			std::cerr << "Possible infinite loop in getFaceVertices for face " << faceIdx << "\n";
#endif
#endif
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

size_t HalfEdgeMesh::countBoundaryEdges() const {
	size_t c = 0;
	for (auto& e : edges) {
		if (halfEdges[e.halfEdgeIdx].opposite == INVALID_INDEX) {
			++c;
		}
	}
	return c;
}

uint32_t HalfEdgeMesh::getEdgeIndexFromHalfEdge(uint32_t halfEdgeIdx) const {
	if (halfEdgeIdx == INVALID_INDEX)
		return INVALID_INDEX;

	// Linear search through all edges to find a match
	for (uint32_t i = 0; i < edges.size(); i++) {
		if (edges[i].halfEdgeIdx == halfEdgeIdx ||
			(halfEdges[halfEdgeIdx].opposite != INVALID_INDEX &&
				edges[i].halfEdgeIdx == halfEdges[halfEdgeIdx].opposite)) {
			return i;
		}
	}

	return INVALID_INDEX;
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
#ifndef NDEBUG
		std::cerr << "Warning: Position count mismatch" << std::endl;
#endif
		return;
	}
	for (size_t i = 0; i < newPositions.size(); ++i) {
		vertices[i].position = newPositions[i];
	}
}

void HalfEdgeMesh::debugPrintStats() const {
#ifndef NDEBUG
	std::cout << "HalfEdgeMesh Statistics:" << std::endl;
	std::cout << "  Vertices: " << vertices.size() << std::endl;
	std::cout << "  Edges: " << edges.size() << std::endl;
	std::cout << "  Faces: " << faces.size() << std::endl;
	std::cout << "  HalfEdges: " << halfEdges.size() << std::endl;
	std::cout << "  Is Manifold: " << (isManifold() ? "Yes" : "No") << std::endl;
#endif

	// Check for boundary edges
	int boundaryEdges = 0;
	for (const auto& edge : edges) {
		if (halfEdges[edge.halfEdgeIdx].opposite == INVALID_INDEX) {
			boundaryEdges++;
		}
	}
#ifndef NDEBUG
	std::cout << "  Boundary Edges: " << boundaryEdges << std::endl;
#endif
}