#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

inline glm::vec3 makeOrthoU(const glm::vec3& n) {
	glm::vec3 a = (std::abs(n.z) < 0.9f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 u = glm::cross(a, n);
	float l2 = glm::dot(u, u);
	
	if (l2 < 1e-20f) {
		a = glm::vec3(0.0f, 1.0f, 0.0f);
		u = glm::cross(a, n);
		l2 = glm::dot(u, u);
		
		if (l2 < 1e-20f) {
			return glm::vec3(1.0f, 0.0f, 0.0f);
		}
	}
	
	return u * (1.0f / std::sqrt(l2));
}

inline glm::vec2 projectToPlane2D(const glm::vec3& p, const glm::vec3& origin, const glm::vec3& u, const glm::vec3& v) {
	glm::vec3 d = p - origin;
	return glm::vec2(glm::dot(d, u), glm::dot(d, v));
}

inline float polygonArea2D(const std::vector<glm::vec2>& poly) {
	if (poly.size() < 3) {
		return 0.0f;
	}

	double a = 0.0;
	
	for (size_t i = 0; i < poly.size(); ++i) {
		const glm::vec2& p = poly[i];
		const glm::vec2& q = poly[(i + 1) % poly.size()];
		a += static_cast<double>(p.x) * static_cast<double>(q.y) - static_cast<double>(p.y) * static_cast<double>(q.x);
	}
	
	return static_cast<float>(0.5 * a);
}

inline bool insideEdge(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
	glm::vec2 e = b - a;
	glm::vec2 w = p - a;
	float crossZ = e.x * w.y - e.y * w.x;
	
	return crossZ >= 0.0f;
}

inline glm::vec2 intersectLines(const glm::vec2& p, const glm::vec2& q, const glm::vec2& a, const glm::vec2& b) {
	glm::vec2 r = q - p;
	glm::vec2 s = b - a;
	float denom = r.x * s.y - r.y * s.x;
	
	if (std::abs(denom) < 1e-20f) {
		return p;
	}
	
	glm::vec2 ap = a - p;
	float t = (ap.x * s.y - ap.y * s.x) / denom;
	
	return p + t * r;
}

inline std::vector<glm::vec2> clipPolygon(const std::vector<glm::vec2>& subject, const std::vector<glm::vec2>& clip) {
	std::vector<glm::vec2> output = subject;
	
	if (output.empty() || clip.size() < 3) {
		return {};
	}
	
	for (size_t i = 0; i < clip.size(); ++i) {
		const glm::vec2& A = clip[i];
		const glm::vec2& B = clip[(i + 1) % clip.size()];
		std::vector<glm::vec2> input = output;
		output.clear();
		if (input.empty()) {
			break;
		}
		glm::vec2 S = input.back();
		for (size_t j = 0; j < input.size(); ++j) {
			glm::vec2 E = input[j];
			bool Ein = insideEdge(E, A, B);
			bool Sin = insideEdge(S, A, B);
			if (Ein) {
				if (!Sin) {
					output.push_back(intersectLines(S, E, A, B));
				}
				output.push_back(E);
			} else if (Sin) {
				output.push_back(intersectLines(S, E, A, B));
			}
			S = E;
		}
	}
	
	return output;
}

inline float computeTriangleOverlapSharedPlane(
	const glm::vec3& r0, const glm::vec3& r1, const glm::vec3& r2, const glm::vec3& nR,
	const glm::vec3& s0, const glm::vec3& s1, const glm::vec3& s2, const glm::vec3& nS,
	std::vector<glm::vec3>* outPolygon = nullptr) {

	glm::vec3 n = nR - nS;
	float n2 = glm::dot(n, n);
	
	if (n2 < 1e-20f) {
		n = nR;
		n2 = glm::dot(n, n);
	}
	
	if (n2 < 1e-20f) {
		return 0.0f;
	}
	
	n *= (1.0f / std::sqrt(n2));
	glm::vec3 u = makeOrthoU(n);
	glm::vec3 v = glm::cross(n, u);

	glm::vec3 origin = r0;
	std::vector<glm::vec2> clipPoly = {
		projectToPlane2D(r0, origin, u, v),
		projectToPlane2D(r1, origin, u, v),
		projectToPlane2D(r2, origin, u, v)
	};
	
	std::vector<glm::vec2> subject = {
		projectToPlane2D(s0, origin, u, v),
		projectToPlane2D(s1, origin, u, v),
		projectToPlane2D(s2, origin, u, v)
	};

	float clipArea = polygonArea2D(clipPoly);
	
	if (clipArea < 0.0f) {
		std::reverse(clipPoly.begin(), clipPoly.end());
	}

	std::vector<glm::vec2> inter = clipPolygon(subject, clipPoly);
	float area = std::abs(polygonArea2D(inter));

	if (outPolygon) {
		outPolygon->clear();
		outPolygon->reserve(inter.size());
		for (const auto& p : inter) {
			outPolygon->push_back(origin + u * p.x + v * p.y);
		}
	}

	return area;
}

inline bool intersectRayTriangle(
	const glm::vec3& orig, const glm::vec3& dir,
	const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
	float& outT, float& outU, float& outV) {

	const float EPS = 1e-8f;
	glm::vec3 e1 = v1 - v0;
	glm::vec3 e2 = v2 - v0;
	glm::vec3 pvec = glm::cross(dir, e2);
	float det = glm::dot(e1, pvec);

	if (std::abs(det) < EPS) {
		return false;
	}

	float invDet = 1.0f / det;
	glm::vec3 tvec = orig - v0;
	float u = glm::dot(tvec, pvec) * invDet;

	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	glm::vec3 qvec = glm::cross(tvec, e1);
	float v = glm::dot(dir, qvec) * invDet;

	if (v < 0.0f || (u + v) > 1.0f) {
		return false;
	}

	float t = glm::dot(e2, qvec) * invDet;
	if (t < 0.0f) {
		return false;
	}
	
	outT = t;
	outU = u;
	outV = v;
	
	return true;
}

inline glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 ap = p - a;

	float d1 = glm::dot(ab, ap);
	float d2 = glm::dot(ac, ap);

	if (d1 <= 0.0f && d2 <= 0.0f) {
		return a;
	}

	glm::vec3 bp = p - b;
	float d3 = glm::dot(ab, bp);
	float d4 = glm::dot(ac, bp);

	if (d3 >= 0.0f && d4 <= d3) {
		return b;
	}

	float vc = d1 * d4 - d3 * d2;

	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		float v = d1 / (d1 - d3);
		return a + v * ab;
	}

	glm::vec3 cp = p - c;
	float d5 = glm::dot(ab, cp);
	float d6 = glm::dot(ac, cp);

	if (d6 >= 0.0f && d5 <= d6) {
		return c;
	}

	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		float w = d2 / (d2 - d6);
		return a + w * ac;
	}

	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + w * (c - b);
	}

	float denom = 1.0f / (va + vb + vc);
	float v = vb * denom;
	float w = vc * denom;
	return a + ab * v + ac * w;
}
