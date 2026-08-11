#pragma once

#include "RVD.hpp"
#include "RVDStatus.cuh"

#include <cuda_runtime.h>

#include <cstdint>

namespace voronoi::rvdDevice {

constexpr uint32_t EndOfList = 0xffffffffu;

struct NormalLimits {
    static constexpr int P = 50;
    static constexpr int V = 50;
};

struct DiscoveryLimits {
    static constexpr int P = int(RVDRetryPlaneCount);
    static constexpr int V = int(RVDRetryVertexCount);
    static constexpr int T = int(RVDRetryTriangleCount);
};

__device__ inline float3 add(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ inline float3 sub(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ inline float3 mul(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
__device__ inline float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ inline float3 cross3(float3 a, float3 b) {
    return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
__device__ inline float lengthSquared(float3 a) { return dot3(a, a); }
__device__ inline float3 min3(float3 a, float3 b) {
    return make_float3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z));
}
__device__ inline float3 max3(float3 a, float3 b) {
    return make_float3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z));
}
__device__ inline bool finite3(float3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }

__device__ inline double determinant4x4Double(float4 p0, float4 p1, float4 p2, float4 equation) {
    const double m12 = double(p1.x) * p0.y - double(p0.x) * p1.y;
    const double m13 = double(p2.x) * p0.y - double(p0.x) * p2.y;
    const double m14 = double(equation.x) * p0.y - double(p0.x) * equation.y;
    const double m23 = double(p2.x) * p1.y - double(p1.x) * p2.y;
    const double m24 = double(equation.x) * p1.y - double(p1.x) * equation.y;
    const double m34 = double(equation.x) * p2.y - double(p2.x) * equation.y;
    const double m123 = m23 * p0.z - m13 * p1.z + m12 * p2.z;
    const double m124 = m24 * p0.z - m14 * p1.z + m12 * equation.z;
    const double m134 = m34 * p0.z - m14 * p2.z + m13 * equation.z;
    const double m234 = m34 * p1.z - m24 * p2.z + m23 * equation.z;
    return m234 * p0.w - m134 * p1.w + m124 * p2.w - m123 * equation.w;
}

__device__ inline float determinant4x4Float(float4 p0, float4 p1, float4 p2, float4 equation,
                                            float& magnitude) {
    const float m12a = p1.x * p0.y, m12b = -p0.x * p1.y;
    const float m13a = p2.x * p0.y, m13b = -p0.x * p2.y;
    const float m14a = equation.x * p0.y, m14b = -p0.x * equation.y;
    const float m23a = p2.x * p1.y, m23b = -p1.x * p2.y;
    const float m24a = equation.x * p1.y, m24b = -p1.x * equation.y;
    const float m34a = equation.x * p2.y, m34b = -p2.x * equation.y;
    const float M12 = fmaxf(fabsf(m12a), fabsf(m12b));
    const float M13 = fmaxf(fabsf(m13a), fabsf(m13b));
    const float M14 = fmaxf(fabsf(m14a), fabsf(m14b));
    const float M23 = fmaxf(fabsf(m23a), fabsf(m23b));
    const float M24 = fmaxf(fabsf(m24a), fabsf(m24b));
    const float M34 = fmaxf(fabsf(m34a), fabsf(m34b));
    const float m12 = m12a + m12b, m13 = m13a + m13b, m14 = m14a + m14b;
    const float m23 = m23a + m23b, m24 = m24a + m24b, m34 = m34a + m34b;
    const float m123 = m23 * p0.z - m13 * p1.z + m12 * p2.z;
    const float m124 = m24 * p0.z - m14 * p1.z + m12 * equation.z;
    const float m134 = m34 * p0.z - m14 * p2.z + m13 * equation.z;
    const float m234 = m34 * p1.z - m24 * p2.z + m23 * equation.z;
    const float M123 = fmaxf(fabsf(M23 * p0.z), fmaxf(fabsf(M13 * p1.z), fabsf(M12 * p2.z)));
    const float M124 = fmaxf(fabsf(M24 * p0.z), fmaxf(fabsf(M14 * p1.z), fabsf(M12 * equation.z)));
    const float M134 = fmaxf(fabsf(M34 * p0.z), fmaxf(fabsf(M14 * p2.z), fabsf(M13 * equation.z)));
    const float M234 = fmaxf(fabsf(M34 * p1.z), fmaxf(fabsf(M24 * p2.z), fabsf(M23 * equation.z)));
    magnitude = fmaxf(fabsf(M234 * p0.w), fmaxf(fabsf(M134 * p1.w),
                      fmaxf(fabsf(M124 * p2.w), fabsf(M123 * equation.w))));
    return m234 * p0.w - m134 * p1.w + m124 * p2.w - m123 * equation.w;
}

template <class Limits>
struct ConvexCellStorage {
    alignas(16) uint3 vertices[Limits::V];
    alignas(16) float4 planes[Limits::P];
    int planeNeighborIds[Limits::P];
    uint32_t boundaryNext[Limits::P];
    float planeAreas[Limits::P];
};

struct RestrictedConvexCellStorage {
    ConvexCellStorage<DiscoveryLimits> cell;
    int32_t uniqueTriangles[DiscoveryLimits::T];
    alignas(16) uint3 backupVertices[DiscoveryLimits::V];
};

template <bool RobustPredicate, bool FilterPredicate>
struct ConvexCell {
    float4* planes = nullptr;
    int* planeNeighborIds = nullptr;
    uint32_t* boundaryNext = nullptr;
    uint3* vertices = nullptr;
    uint3* backupVertices = nullptr;
    float* planeAreas = nullptr;
    int32_t* uniqueTriangles = nullptr;
    uint32_t planeCapacity = 0;
    uint32_t vertexCapacity = 0;
    uint32_t triangleCapacity = 0;
    int vertexCount = 0;
    int planeCount = 0;
    RVDStatus status = RVDStatus::Success;

    template <class Limits>
    __device__ void attachStorage(ConvexCellStorage<Limits>& storage,
                                  uint32_t planeLimit = uint32_t(Limits::P),
                                  uint32_t vertexLimit = uint32_t(Limits::V)) {
        planes = storage.planes;
        planeNeighborIds = storage.planeNeighborIds;
        boundaryNext = storage.boundaryNext;
        vertices = storage.vertices;
        planeAreas = storage.planeAreas;
        backupVertices = nullptr;
        uniqueTriangles = nullptr;
        planeCapacity = planeLimit > uint32_t(Limits::P) ? uint32_t(Limits::P) : planeLimit;
        vertexCapacity = vertexLimit > uint32_t(Limits::V) ? uint32_t(Limits::V) : vertexLimit;
        triangleCapacity = 0;
    }

    __device__ void attachStorage(RestrictedConvexCellStorage& storage,
                                  uint32_t planeLimit,
                                  uint32_t vertexLimit,
                                  uint32_t triangleLimit) {
        attachStorage(storage.cell, planeLimit, vertexLimit);
        backupVertices = storage.backupVertices;
        uniqueTriangles = storage.uniqueTriangles;
        triangleCapacity = triangleLimit > uint32_t(DiscoveryLimits::T)
            ? uint32_t(DiscoveryLimits::T)
            : triangleLimit;
    }

    __device__ void fail(RVDStatus value) {
        if (status == RVDStatus::Success) status = value;
    }

    __device__ void initialize(const float4* domainPlanes) {
        for (uint32_t i = 0; i < planeCapacity; ++i) {
            planeNeighborIds[i] = -1;
            planeAreas[i] = 0.0f;
        }
        for (uint32_t i = 0; i < 6; ++i) planes[i] = domainPlanes[i];
        planeCount = 6;
        vertices[0] = make_uint3(2, 5, 0); vertices[1] = make_uint3(5, 3, 0);
        vertices[2] = make_uint3(1, 5, 2); vertices[3] = make_uint3(5, 1, 3);
        vertices[4] = make_uint3(4, 2, 0); vertices[5] = make_uint3(4, 0, 3);
        vertices[6] = make_uint3(2, 4, 1); vertices[7] = make_uint3(4, 3, 1);
        vertexCount = 8;
    }

    __device__ uint32_t planeAt(uint3 triplet, int index) const {
        return index == 0 ? triplet.x : (index == 1 ? triplet.y : triplet.z);
    }

    __device__ bool compactPlanes() {
        if (status != RVDStatus::Success || vertexCount <= 0) return false;
        for (int plane = 0; plane < planeCount; ++plane) boundaryNext[plane] = EndOfList;
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            const uint3 triplet = vertices[vertex];
            if (triplet.x >= uint32_t(planeCount) || triplet.y >= uint32_t(planeCount) ||
                triplet.z >= uint32_t(planeCount)) {
                fail(RVDStatus::InvalidIndex);
                return false;
            }
            boundaryNext[triplet.x] = 0;
            boundaryNext[triplet.y] = 0;
            boundaryNext[triplet.z] = 0;
        }

        uint32_t compactCount = 0;
        for (int oldPlane = 0; oldPlane < planeCount; ++oldPlane) {
            if (boundaryNext[oldPlane] == EndOfList) continue;
            if (uint32_t(oldPlane) != compactCount) {
                planes[compactCount] = planes[oldPlane];
                planeNeighborIds[compactCount] = planeNeighborIds[oldPlane];
            }
            boundaryNext[oldPlane] = compactCount++;
        }
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            const uint3 old = vertices[vertex];
            vertices[vertex] = make_uint3(boundaryNext[old.x], boundaryNext[old.y],
                                          boundaryNext[old.z]);
        }
        planeCount = int(compactCount);
        return status == RVDStatus::Success;
    }

    __device__ float3 vertexPosition(uint3 triplet) {
        if (triplet.x >= uint32_t(planeCount) || triplet.y >= uint32_t(planeCount) ||
            triplet.z >= uint32_t(planeCount)) {
            fail(RVDStatus::InvalidIndex);
            return make_float3(0, 0, 0);
        }
        const float4 p0 = planes[triplet.x], p1 = planes[triplet.y], p2 = planes[triplet.z];
        const double m12 = double(p1.x) * p2.y - double(p1.y) * p2.x;
        const double m13 = double(p1.x) * p2.z - double(p1.z) * p2.x;
        const double m14 = double(p1.x) * p2.w - double(p1.w) * p2.x;
        const double m23 = double(p1.y) * p2.z - double(p1.z) * p2.y;
        const double m24 = double(p1.y) * p2.w - double(p1.w) * p2.y;
        const double m34 = double(p1.z) * p2.w - double(p1.w) * p2.z;
        const double x = -double(p0.w) * m23 - double(p0.y) * m34 + double(p0.z) * m24;
        const double y = double(p0.x) * m34 + double(p0.w) * m13 - double(p0.z) * m14;
        const double z = -double(p0.x) * m24 + double(p0.y) * m14 - double(p0.w) * m12;
        const double w = double(p0.x) * m23 - double(p0.y) * m13 + double(p0.z) * m12;
        return make_float3(float(x / w), float(y / w), float(z / w));
    }

    __device__ bool vertexOutside(uint3 triplet, float4 equation) {
        if (triplet.x >= uint32_t(planeCount) || triplet.y >= uint32_t(planeCount) ||
            triplet.z >= uint32_t(planeCount)) {
            fail(RVDStatus::InvalidIndex);
            return false;
        }
        const float4 p0 = planes[triplet.x], p1 = planes[triplet.y], p2 = planes[triplet.z];
        if constexpr (RobustPredicate) {
            const double determinant = determinant4x4Double(p0, p1, p2, equation);
            return determinant > 0.0;
        } else {
            float magnitude = 0.0f;
            const float determinant = determinant4x4Float(p0, p1, p2, equation, magnitude);
            if constexpr (FilterPredicate) {
                if (fabsf(determinant) < 3.0e-6f * magnitude) {
                    fail(RVDStatus::NeedsRobustPredicate);
                    return determinant > -2.0e-7f * magnitude;
                }
            }
            return determinant > 0.0f;
        }
    }

    __device__ bool newVertex(uint32_t a, uint32_t b, uint32_t c) {
        if (a >= uint32_t(planeCount) || b >= uint32_t(planeCount) || c >= uint32_t(planeCount)) {
            fail(RVDStatus::InvalidIndex);
            return false;
        }
        if (vertexCount >= int(vertexCapacity)) {
            fail(RVDStatus::VertexOverflow);
            return false;
        }
        vertices[vertexCount++] = make_uint3(a, b, c);
        return true;
    }

    __device__ uint32_t computeBoundary(int removedCount) {
        for (uint32_t i = 0; i < planeCapacity; ++i) boundaryNext[i] = EndOfList;
        uint32_t firstBoundary = EndOfList;
        int iterations = 0;
        int triangle = vertexCount;
        while (removedCount > 0) {
            if (iterations++ > 1000) {
                fail(RVDStatus::InconsistentBoundary);
                return firstBoundary;
            }
            bool isInBorder[3];
            bool nextIsOpposite[3];
            for (int edge = 0; edge < 3; ++edge)
                isInBorder[edge] = boundaryNext[planeAt(vertices[triangle], edge)] != EndOfList;
            for (int edge = 0; edge < 3; ++edge)
                nextIsOpposite[edge] = boundaryNext[planeAt(vertices[triangle], (edge + 1) % 3)] ==
                                       planeAt(vertices[triangle], edge);
            bool simple = true;
            for (int edge = 0; edge < 3; ++edge) {
                if (!nextIsOpposite[edge] && !nextIsOpposite[(edge + 1) % 3] &&
                    isInBorder[(edge + 1) % 3]) simple = false;
            }
            if (!nextIsOpposite[0] && !nextIsOpposite[1] && !nextIsOpposite[2]) {
                if (firstBoundary == EndOfList) {
                    for (int edge = 0; edge < 3; ++edge)
                        boundaryNext[planeAt(vertices[triangle], edge)] =
                            planeAt(vertices[triangle], (edge + 1) % 3);
                    firstBoundary = vertices[triangle].x;
                } else simple = false;
            }
            if (!simple) {
                ++triangle;
                if (triangle == vertexCount + removedCount) triangle = vertexCount;
                continue;
            }
            for (int edge = 0; edge < 3; ++edge) {
                if (!nextIsOpposite[edge])
                    boundaryNext[planeAt(vertices[triangle], edge)] =
                        planeAt(vertices[triangle], (edge + 1) % 3);
            }
            for (int edge = 0; edge < 3; ++edge) {
                if (nextIsOpposite[edge] && nextIsOpposite[(edge + 1) % 3]) {
                    const uint32_t plane = planeAt(vertices[triangle], (edge + 1) % 3);
                    if (firstBoundary == plane) firstBoundary = boundaryNext[plane];
                    boundaryNext[plane] = EndOfList;
                }
            }
            const uint3 temporary = vertices[triangle];
            vertices[triangle] = vertices[vertexCount + removedCount - 1];
            vertices[vertexCount + removedCount - 1] = temporary;
            triangle = vertexCount;
            --removedCount;
        }
        return firstBoundary;
    }

    __device__ bool clipLastPlane(bool* changed = nullptr) {
        if (changed) *changed = false;
        if (planeCount <= 0 || uint32_t(planeCount) > planeCapacity) {
            fail(RVDStatus::InvalidIndex);
            return false;
        }
        const uint32_t newPlane = uint32_t(planeCount - 1);
        const float4 equation = planes[newPlane];
        int removedCount = 0;
        int vertex = 0;
        while (vertex < vertexCount) {
            if (vertexOutside(vertices[vertex], equation)) {
                --vertexCount;
                const uint3 temp = vertices[vertex];
                vertices[vertex] = vertices[vertexCount];
                vertices[vertexCount] = temp;
                ++removedCount;
            } else {
                ++vertex;
            }
        }
        if (status != RVDStatus::Success) return false;
        if (vertexCount == 0) {
            fail(RVDStatus::EmptyCell);
            return false;
        }
        if (removedCount == 0) {
            --planeCount;
            return true;
        }
        if (changed) *changed = true;
        const uint32_t firstBoundary = computeBoundary(removedCount);
        if (status != RVDStatus::Success) return false;
        if (firstBoundary == EndOfList) return true;
        uint32_t current = firstBoundary;
        do {
            const uint32_t next = boundaryNext[current];
            if (!newVertex(newPlane, current, next)) return false;
            current = next;
        } while (current != firstBoundary);
        return true;
    }

    __device__ bool addPlane(float4 plane, int neighborId = -1, bool* changed = nullptr) {
        if (uint32_t(planeCount) >= planeCapacity) {
            fail(RVDStatus::PlaneOverflow);
            return false;
        }
        if (!isfinite(plane.x) || !isfinite(plane.y) || !isfinite(plane.z) || !isfinite(plane.w)) {
            fail(RVDStatus::NonFiniteGeometry);
            return false;
        }
        planes[planeCount] = plane;
        planeNeighborIds[planeCount] = neighborId;
        ++planeCount;
        return clipLastPlane(changed);
    }

    __device__ float maximumVertexDistanceSquared(float3 seed) {
        float maximum = 0.0f;
        for (int i = 0; i < vertexCount && status == RVDStatus::Success; ++i) {
            const float3 vertex = vertexPosition(vertices[i]);
            const float distanceSquared = lengthSquared(sub(vertex, seed));
            if (distanceSquared > maximum) maximum = distanceSquared;
        }
        return maximum;
    }
};

__device__ inline float determinant3(float3 a, float3 b, float3 c) {
    return a.x * (b.y * c.z - b.z * c.y) - b.x * (a.y * c.z - a.z * c.y) +
           c.x * (a.y * b.z - a.z * b.y);
}

__device__ inline double determinant3Double(float3 a, float3 b, float3 c) {
    return double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) -
           double(b.x) * (double(a.y) * c.z - double(a.z) * c.y) +
           double(c.x) * (double(a.y) * b.z - double(a.z) * b.y);
}

__device__ inline float4 planeFromPoints(float3 a, float3 b, float3 c) {
    const float3 normal = cross3(sub(b, a), sub(c, a));
    return make_float4(normal.x, normal.y, normal.z, -dot3(normal, a));
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ float4 integrateCell(ConvexCell<RobustPredicate, FilterPredicate>& cell, float3 seed,
                                int staticPlaneCount, float factor) {
    float4 bary = make_float4(0, 0, 0, 0);
    for (int vertex = 0; vertex < cell.vertexCount && cell.status == RVDStatus::Success; ++vertex) {
        const float3 a = cell.vertexPosition(cell.vertices[vertex]);
        const float3 ac = sub(seed, a);
        const uint3 planeIds = cell.vertices[vertex];
        const uint32_t ids[3] = {planeIds.x, planeIds.y, planeIds.z};
        float3 projected[3];
        float3 lines[3];
        for (int i = 0; i < 3; ++i) {
            const float4 p = cell.planes[ids[i]];
            const float3 normal = make_float3(p.x, p.y, p.z);
            const float n2 = lengthSquared(normal);
            projected[i] = n2 > 1e-30f ? sub(ac, mul(normal, dot3(ac, normal) / n2)) : make_float3(0, 0, 0);
        }
        for (int i = 0; i < 3; ++i) {
            const float4 p0 = cell.planes[ids[i]];
            const float4 p1 = cell.planes[ids[(i + 1) % 3]];
            const float3 line = cross3(make_float3(p0.x, p0.y, p0.z), make_float3(p1.x, p1.y, p1.z));
            const float line2 = lengthSquared(line);
            lines[i] = line2 > 1e-30f ? mul(line, dot3(ac, line) / line2) : make_float3(0, 0, 0);
        }
        for (int i = 0; i < 3; ++i) {
            for (int step = 0; step < 2; ++step) {
                const int j = (i + 3 - step) % 3;
                const float sign = step ? 1.0f : -1.0f;
                const float weight = sign * determinant3(ac, projected[i], lines[j]);
                bary.w += weight;
                const float3 sum = add(add(ac, projected[i]), lines[j]);
                bary.x += weight * (a.x + 0.25f * sum.x);
                bary.y += weight * (a.y + 0.25f * sum.y);
                bary.z += weight * (a.z + 0.25f * sum.z);
                if (ids[i] < uint32_t(staticPlaneCount) &&
                    cell.planeNeighborIds[ids[i]] >= 0) {
                    const float4 plane = cell.planes[ids[i]];
                    const float3 normal = make_float3(plane.x, plane.y, plane.z);
                    cell.planeAreas[ids[i]] +=
                        float(0.5 * double(sign) *
                              determinant3Double(projected[i], lines[j], normal)) * factor;
                }
            }
        }
    }
    if (!isfinite(bary.x) || !isfinite(bary.y) || !isfinite(bary.z) || !isfinite(bary.w)) {
        cell.fail(RVDStatus::NonFiniteGeometry);
    }
    return bary;
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ float clippedTriangleArea(float3 v0, float3 v1, float3 v2,
                                     const ConvexCell<RobustPredicate, FilterPredicate>& cell,
                                     int staticPlaneCount) {
    float3 polygon[64];
    float3 clipped[64];
    polygon[0] = v0; polygon[1] = v1; polygon[2] = v2;
    int polygonCount = 3;
    for (int planeIndex = 0; planeIndex < staticPlaneCount && polygonCount >= 3; ++planeIndex) {
        const float4 plane = cell.planes[planeIndex];
        int clippedCount = 0;
        float3 previous = polygon[polygonCount - 1];
        float previousValue = plane.x * previous.x + plane.y * previous.y + plane.z * previous.z + plane.w;
        bool previousInside = previousValue >= -1e-6f;
        for (int vertex = 0; vertex < polygonCount; ++vertex) {
            const float3 current = polygon[vertex];
            const float currentValue = plane.x * current.x + plane.y * current.y + plane.z * current.z + plane.w;
            const bool currentInside = currentValue >= -1e-6f;
            if (currentInside != previousInside && clippedCount < 64) {
                const float denominator = previousValue - currentValue;
                const float t = fabsf(denominator) > 1e-12f ? fminf(fmaxf(previousValue / denominator, 0.0f), 1.0f) : 0.0f;
                clipped[clippedCount++] = add(previous, mul(sub(current, previous), t));
            }
            if (currentInside && clippedCount < 64) clipped[clippedCount++] = current;
            previous = current;
            previousValue = currentValue;
            previousInside = currentInside;
        }
        polygonCount = clippedCount;
        for (int i = 0; i < polygonCount; ++i) polygon[i] = clipped[i];
    }
    if (polygonCount < 3) return 0.0f;
    float doubleArea = 0.0f;
    for (int i = 1; i + 1 < polygonCount; ++i) {
        doubleArea += sqrtf(lengthSquared(cross3(sub(polygon[i], polygon[0]), sub(polygon[i + 1], polygon[0]))));
    }
    return 0.5f * doubleArea;
}

} // namespace voronoi::rvdDevice
