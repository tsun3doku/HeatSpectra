#include "ContactSampling.hpp"
#include "util/GeometryUtils.hpp"

#include "mesh/remesher/iODT.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "spatial/TriangleHashGrid.hpp"

#include <cmath>
#include <limits>
#include <algorithm>

namespace {

glm::vec3 normalizedOrZero(const glm::vec3& vector) {
    const float lengthSquared = glm::dot(vector, vector);
    if (lengthSquared <= 1e-12f) {
        return glm::vec3(0.0f);
    }
    return vector * (1.0f / std::sqrt(lengthSquared));
}

glm::mat4 toMat4(const std::array<float, 16>& values) {
    glm::mat4 matrix(1.0f);
    matrix[0][0] = values[0];
    matrix[0][1] = values[1];
    matrix[0][2] = values[2];
    matrix[0][3] = values[3];
    matrix[1][0] = values[4];
    matrix[1][1] = values[5];
    matrix[1][2] = values[6];
    matrix[1][3] = values[7];
    matrix[2][0] = values[8];
    matrix[2][1] = values[9];
    matrix[2][2] = values[10];
    matrix[2][3] = values[11];
    matrix[3][0] = values[12];
    matrix[3][1] = values[13];
    matrix[3][2] = values[14];
    matrix[3][3] = values[15];
    return matrix;
}

}

void mapSurfacePoints(
    const SupportingHalfedge::IntrinsicMesh& sourceMesh,
    const std::array<float, 16>& sourceLocalToWorld,
    const std::vector<const SupportingHalfedge::IntrinsicMesh*>& receiverIntrinsicMeshes,
    const std::vector<std::array<float, 16>>& receiverLocalToWorld,
    std::vector<std::vector<ContactPair>>& receiverContactPairs,
    std::vector<ContactLineVertex>& outOutlineVertices,
    std::vector<ContactLineVertex>& outCorrespondenceVertices,
    float contactRadius,
    float minNormalDot) {
    receiverContactPairs.clear();
    outOutlineVertices.clear();
    outCorrespondenceVertices.clear();

	if (sourceMesh.triangles.empty()) {
		return;
	}

    std::vector<glm::vec3> sourcePositions;
    sourcePositions.reserve(sourceMesh.vertices.size());
    glm::vec3 sourceBoundsMin(std::numeric_limits<float>::infinity());
    glm::vec3 sourceBoundsMax(-std::numeric_limits<float>::infinity());
    for (const SupportingHalfedge::IntrinsicVertex& vertex : sourceMesh.vertices) {
        sourcePositions.push_back(vertex.position);
        sourceBoundsMin = glm::min(sourceBoundsMin, vertex.position);
        sourceBoundsMax = glm::max(sourceBoundsMax, vertex.position);
    }

    const float contactPadding = std::max(contactRadius, 1e-4f);
    const glm::vec3 contactPaddingVec(contactPadding);
    TriangleHashGrid sourceTriangleGrid;
    sourceTriangleGrid.build(
        sourcePositions,
        sourceMesh.indices,
        sourceBoundsMin - contactPaddingVec,
        sourceBoundsMax + contactPaddingVec,
        contactPadding);

    std::vector<size_t> candidateTriangles;

    glm::mat4 srcModelMat = toMat4(sourceLocalToWorld);
    glm::mat4 invSrcModelMat = glm::inverse(srcModelMat);
	glm::mat3 srcNormalMat = glm::transpose(glm::inverse(glm::mat3(srcModelMat)));

	receiverContactPairs.resize(receiverIntrinsicMeshes.size());

	for (size_t receiverIdx = 0; receiverIdx < receiverIntrinsicMeshes.size(); receiverIdx++) {
        if (receiverIdx >= receiverLocalToWorld.size()) {
            continue;
        }

		const SupportingHalfedge::IntrinsicMesh* receiverMesh = receiverIntrinsicMeshes[receiverIdx];
		if (!receiverMesh) {
			continue;
		}
		receiverContactPairs[receiverIdx].assign(receiverMesh->triangles.size(), ContactPair{});

		glm::mat4 recvModelMat = toMat4(receiverLocalToWorld[receiverIdx]);
		glm::mat3 recvNormalMat = glm::transpose(glm::inverse(glm::mat3(recvModelMat)));

		for (size_t triIdx = 0; triIdx < receiverMesh->triangles.size(); triIdx++) {
			const auto& rTri = receiverMesh->triangles[triIdx];
			uint32_t rv0 = rTri.vertexIndices[0];
			uint32_t rv1 = rTri.vertexIndices[1];
			uint32_t rv2 = rTri.vertexIndices[2];
			if (rv0 >= receiverMesh->vertices.size() || rv1 >= receiverMesh->vertices.size() || rv2 >= receiverMesh->vertices.size()) {
				continue;
			}

			const glm::vec3 p0 = receiverMesh->vertices[rv0].position;
			const glm::vec3 p1 = receiverMesh->vertices[rv1].position;
			const glm::vec3 p2 = receiverMesh->vertices[rv2].position;

			const glm::vec3 n0 = receiverMesh->vertices[rv0].normal;
			const glm::vec3 n1 = receiverMesh->vertices[rv1].normal;
			const glm::vec3 n2 = receiverMesh->vertices[rv2].normal;

			ContactPair pair{};
			for (uint32_t si = 0; si < Quadrature::count; ++si) {
				pair.samples[si].sourceTriangleIndex = iODT::INVALID_INDEX;
				pair.samples[si].u = 0.0f;
				pair.samples[si].v = 0.0f;
				pair.samples[si].wArea = 0.0f;
			}
			pair.contactArea = 0.0f;

			const float recvArea = rTri.area;
			if (recvArea <= 0.0f) {
				receiverContactPairs[receiverIdx][triIdx] = pair;
				continue;
			}

			for (uint32_t si = 0; si < Quadrature::count; ++si) {
				const glm::vec3 barycentricCoord = Quadrature::bary[si];
				glm::vec3 localPos = p0 * barycentricCoord.x + p1 * barycentricCoord.y + p2 * barycentricCoord.z;
				glm::vec3 localN = n0 * barycentricCoord.x + n1 * barycentricCoord.y + n2 * barycentricCoord.z;
				float localNLen2 = glm::dot(localN, localN);
				if (localNLen2 < 1e-12f) {
					continue;
				}
				localN *= (1.0f / std::sqrt(localNLen2));

				glm::vec3 worldPos = glm::vec3(recvModelMat * glm::vec4(localPos, 1.0f));
				glm::vec3 worldN = normalizedOrZero(glm::vec3(recvNormalMat * localN));
				if (glm::dot(worldN, worldN) < 1e-12f) {
					continue;
				}

				glm::vec3 srcPos = glm::vec3(invSrcModelMat * glm::vec4(worldPos, 1.0f));
				glm::vec3 srcDirPlus = normalizedOrZero(glm::vec3(invSrcModelMat * glm::vec4(worldN, 0.0f)));
				if (glm::dot(srcDirPlus, srcDirPlus) < 1e-12f) {
					continue;
				}

				uint32_t bestTriIdx = iODT::INVALID_INDEX;
				float bestT = std::numeric_limits<float>::infinity();
				float bestU = 0.0f;
				float bestV = 0.0f;

                candidateTriangles.clear();
                sourceTriangleGrid.getTrianglesAlongRay(
                    srcPos,
                    srcDirPlus,
                    contactRadius,
                    candidateTriangles);

				for (size_t candidateTriIdx : candidateTriangles) {
                    const uint32_t sTriIdx = static_cast<uint32_t>(candidateTriIdx);
                    if (sTriIdx >= sourceMesh.triangles.size()) {
                        continue;
                    }
					const auto& sTri = sourceMesh.triangles[sTriIdx];
					uint32_t i0 = sTri.vertexIndices[0];
					uint32_t i1 = sTri.vertexIndices[1];
					uint32_t i2 = sTri.vertexIndices[2];
					if (i0 >= sourceMesh.vertices.size() || i1 >= sourceMesh.vertices.size() || i2 >= sourceMesh.vertices.size()) {
						continue;
					}

					glm::vec3 sNWorld = normalizedOrZero(glm::vec3(srcNormalMat * sTri.normal));
					float nd = glm::dot(worldN, sNWorld);
					if (minNormalDot < 0.0f) {
						if (nd > minNormalDot) {
							continue;
						}
					} else {
						if (nd < minNormalDot) {
							continue;
						}
					}

					const glm::vec3& v0 = sourceMesh.vertices[i0].position;
					const glm::vec3& v1 = sourceMesh.vertices[i1].position;
					const glm::vec3& v2 = sourceMesh.vertices[i2].position;

					float t = 0.0f;
					float u = 0.0f;
					float v = 0.0f;
					if (!intersectRayTriangle(srcPos, srcDirPlus, v0, v1, v2, t, u, v)) {
						continue;
					}
					if (t > contactRadius) {
						continue;
					}
					if (t < bestT) {
						bestT = t;
						bestTriIdx = sTriIdx;
						bestU = u;
						bestV = v;
					}
				}

				if (bestTriIdx != iODT::INVALID_INDEX) {
					pair.samples[si].sourceTriangleIndex = bestTriIdx;
					pair.samples[si].u = bestU;
					pair.samples[si].v = bestV;
					float weightedArea = Quadrature::weights[si] * recvArea;
					pair.samples[si].wArea = weightedArea;
					pair.contactArea += weightedArea;
				}
			}

			receiverContactPairs[receiverIdx][triIdx] = pair;
		}

	}

	outOutlineVertices.reserve(receiverIntrinsicMeshes.size() * 256);
	outCorrespondenceVertices.reserve(receiverIntrinsicMeshes.size() * 256);

	for (size_t receiverIdx = 0; receiverIdx < receiverIntrinsicMeshes.size(); receiverIdx++) {
        if (receiverIdx >= receiverLocalToWorld.size()) {
            continue;
        }

		const SupportingHalfedge::IntrinsicMesh* receiverMesh = receiverIntrinsicMeshes[receiverIdx];
		if (!receiverMesh) {
			continue;
		}
		if (receiverIdx >= receiverContactPairs.size()) {
			continue;
		}
		const auto& contactPairs = receiverContactPairs[receiverIdx];

		glm::mat4 recvModel = toMat4(receiverLocalToWorld[receiverIdx]);
		glm::mat4 srcModelMat2 = toMat4(sourceLocalToWorld);

		for (size_t triIdx = 0; triIdx < receiverMesh->triangles.size(); triIdx++) {
			if (triIdx >= contactPairs.size()) {
				continue;
			}
			const ContactPair& pair = contactPairs[triIdx];
			if (pair.contactArea <= 0.0f) {
				continue;
			}

			const auto& rTri = receiverMesh->triangles[triIdx];
			uint32_t rv0 = rTri.vertexIndices[0];
			uint32_t rv1 = rTri.vertexIndices[1];
			uint32_t rv2 = rTri.vertexIndices[2];
			if (rv0 >= receiverMesh->vertices.size() || rv1 >= receiverMesh->vertices.size() || rv2 >= receiverMesh->vertices.size()) {
				continue;
			}

			glm::vec3 r0 = glm::vec3(recvModel * glm::vec4(receiverMesh->vertices[rv0].position, 1.0f));
			glm::vec3 r1 = glm::vec3(recvModel * glm::vec4(receiverMesh->vertices[rv1].position, 1.0f));
			glm::vec3 r2 = glm::vec3(recvModel * glm::vec4(receiverMesh->vertices[rv2].position, 1.0f));

			float contactRatio = 0.0f;
			if (rTri.area > 0.0f) {
				contactRatio = pair.contactArea / rTri.area;
			}

			glm::vec3 patchColor = glm::vec3(0.1f, 0.4f, 1.0f);
			if (contactRatio > 0.95f) {
				patchColor = glm::vec3(1.0f, 0.2f, 0.1f);
			}
			ContactLineVertex l0{};
			l0.position = r0;
			l0.color = patchColor;
			ContactLineVertex l1{};
			l1.position = r1;
			l1.color = patchColor;
			outOutlineVertices.push_back(l0);
			outOutlineVertices.push_back(l1);

			ContactLineVertex l2{};
			l2.position = r1;
			l2.color = patchColor;
			ContactLineVertex l3{};
			l3.position = r2;
			l3.color = patchColor;
			outOutlineVertices.push_back(l2);
			outOutlineVertices.push_back(l3);

			ContactLineVertex l4{};
			l4.position = r2;
			l4.color = patchColor;
			ContactLineVertex l5{};
			l5.position = r0;
			l5.color = patchColor;
			outOutlineVertices.push_back(l4);
			outOutlineVertices.push_back(l5);

			glm::vec3 triN = glm::cross(r1 - r0, r2 - r0);
			float triNLen2 = glm::dot(triN, triN);
			if (triNLen2 > 1e-12f) {
				triN *= (1.0f / std::sqrt(triNLen2));
			}
			glm::vec3 uDir = r1 - r0;
			float uLen2 = glm::dot(uDir, uDir);
			if (uLen2 > 1e-12f) {
				uDir *= (1.0f / std::sqrt(uLen2));
			}
			glm::vec3 vDir = glm::cross(triN, uDir);
			float vLen2 = glm::dot(vDir, vDir);
			if (vLen2 > 1e-12f) {
				vDir *= (1.0f / std::sqrt(vLen2));
			}
			float crossLen = std::sqrt(std::max(1e-12f, rTri.area)) * 0.1f;

			for (uint32_t si = 0; si < Quadrature::count; ++si) {
				const ContactSampleGPU& s = pair.samples[si];
				if (s.sourceTriangleIndex == iODT::INVALID_INDEX || s.wArea <= 0.0f) {
					continue;
				}
				if (s.sourceTriangleIndex >= sourceMesh.triangles.size()) {
					continue;
				}

				const glm::vec3 barycentricCoord = Quadrature::bary[si];
				glm::vec3 recvSampleP = r0 * barycentricCoord.x + r1 * barycentricCoord.y + r2 * barycentricCoord.z;

				const auto& sTri = sourceMesh.triangles[s.sourceTriangleIndex];
				uint32_t si0 = sTri.vertexIndices[0];
				uint32_t si1 = sTri.vertexIndices[1];
				uint32_t si2 = sTri.vertexIndices[2];
				if (si0 >= sourceMesh.vertices.size() || si1 >= sourceMesh.vertices.size() || si2 >= sourceMesh.vertices.size()) {
					continue;
				}

				glm::vec3 sv0 = glm::vec3(srcModelMat2 * glm::vec4(sourceMesh.vertices[si0].position, 1.0f));
				glm::vec3 sv1 = glm::vec3(srcModelMat2 * glm::vec4(sourceMesh.vertices[si1].position, 1.0f));
				glm::vec3 sv2 = glm::vec3(srcModelMat2 * glm::vec4(sourceMesh.vertices[si2].position, 1.0f));
				glm::vec3 sb = glm::vec3(1.0f - s.u - s.v, s.u, s.v);
				glm::vec3 srcHitP = sv0 * sb.x + sv1 * sb.y + sv2 * sb.z;

				ContactLineVertex a{};
				a.position = recvSampleP;
				a.color = glm::vec3(0.9f, 0.9f, 0.2f);
				ContactLineVertex b0{};
				b0.position = srcHitP;
				b0.color = glm::vec3(1.0f, 0.1f, 0.2f);
				outCorrespondenceVertices.push_back(a);
				outCorrespondenceVertices.push_back(b0);

				ContactLineVertex c0{};
				c0.position = recvSampleP - uDir * crossLen;
				c0.color = glm::vec3(0.9f, 0.9f, 0.2f);
				ContactLineVertex c1{};
				c1.position = recvSampleP + uDir * crossLen;
				c1.color = glm::vec3(0.9f, 0.9f, 0.2f);
				outCorrespondenceVertices.push_back(c0);
				outCorrespondenceVertices.push_back(c1);

				ContactLineVertex d0{};
				d0.position = recvSampleP - vDir * crossLen;
				d0.color = glm::vec3(0.9f, 0.9f, 0.2f);
				ContactLineVertex d1{};
				d1.position = recvSampleP + vDir * crossLen;
				d1.color = glm::vec3(0.9f, 0.9f, 0.2f);
				outCorrespondenceVertices.push_back(d0);
				outCorrespondenceVertices.push_back(d1);
			}
		}
	}

}