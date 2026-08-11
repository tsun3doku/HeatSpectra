#pragma once

#include <cstdint>

namespace voronoi {

enum class RVDStatus : uint32_t {
    Success,
    RestrictedPending,
    Ghost,
    EmptyCell,
    VertexOverflow,
    PlaneOverflow,
    TriangleOverflow,
    InterfaceOverflow,
    SecurityRadiusNotReached,
    NeedsRobustPredicate,
    InconsistentBoundary,
    FindAnotherBeginningVertex,
    NonFiniteGeometry,
    InvalidIndex,
    Count
};

#if defined(__CUDACC__)
#define RVD_STATUS_HD __host__ __device__
#else
#define RVD_STATUS_HD
#endif

RVD_STATUS_HD constexpr uint32_t rvdStatusBit(RVDStatus status) {
    return 1u << static_cast<uint32_t>(status);
}

#undef RVD_STATUS_HD

inline constexpr uint32_t RVDRestrictedPassMask =
    rvdStatusBit(RVDStatus::RestrictedPending) |
    rvdStatusBit(RVDStatus::VertexOverflow) |
    rvdStatusBit(RVDStatus::PlaneOverflow) |
    rvdStatusBit(RVDStatus::TriangleOverflow) |
    rvdStatusBit(RVDStatus::SecurityRadiusNotReached);

inline constexpr uint32_t RVDPredicateRetryMask =
    rvdStatusBit(RVDStatus::NeedsRobustPredicate) |
    rvdStatusBit(RVDStatus::InconsistentBoundary) |
    rvdStatusBit(RVDStatus::NonFiniteGeometry);

inline constexpr uint32_t RVDTerminalFailureMask =
    rvdStatusBit(RVDStatus::RestrictedPending) |
    rvdStatusBit(RVDStatus::VertexOverflow) |
    rvdStatusBit(RVDStatus::PlaneOverflow) |
    rvdStatusBit(RVDStatus::TriangleOverflow) |
    rvdStatusBit(RVDStatus::InterfaceOverflow) |
    rvdStatusBit(RVDStatus::SecurityRadiusNotReached) |
    rvdStatusBit(RVDStatus::NeedsRobustPredicate) |
    rvdStatusBit(RVDStatus::InconsistentBoundary) |
    rvdStatusBit(RVDStatus::FindAnotherBeginningVertex) |
    rvdStatusBit(RVDStatus::NonFiniteGeometry) |
    rvdStatusBit(RVDStatus::InvalidIndex);

} // namespace voronoi
