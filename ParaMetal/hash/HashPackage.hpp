#pragma once

#include "hash/HashValues.hpp"

//                                                          [ HashPackage
//                                                            -Centralized package sealing
//                                                            -Computes domain specific hash values for each
//                                                            runtime package type
//                                                            -All package hash construction flows through here ]

struct ModelPackage;
struct RemeshPackage;
struct VoronoiPackage;
struct PointPackage;
struct HeatPackage;

class HashPackage {
public:
    static void seal(ModelPackage& pkg, const HashValues& sourceGeometryHashes, const HashValues& outputHashes);
    static void seal(RemeshPackage& pkg, const HashValues& sourceGeometryHashes);
    static void seal(VoronoiPackage& pkg, const HashValues& authoredHashes);
    static void seal(PointPackage& pkg, const HashValues& sourceHashes);
    static void seal(HeatPackage& pkg, const HashValues& authoredHashes);
};
